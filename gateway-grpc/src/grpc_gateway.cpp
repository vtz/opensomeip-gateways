/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#include "opensomeip/gateway/grpc/grpc_gateway.h"

#include <fstream>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/server_builder.h>
#include <sstream>

#include "gateway.grpc.pb.h"
#include "someip/types.h"

namespace opensomeip {
namespace gateway {
namespace grpc {
namespace detail {

class SomeIpGatewayGrpcService final : public ::opensomeip::gateway::grpc::v1::SomeIpGateway::Service {
public:
    explicit SomeIpGatewayGrpcService(GrpcGateway* owner) : owner_(owner) {}

    ::grpc::Status UnarySomeIp(::grpc::ServerContext* context,
                                const ::opensomeip::gateway::grpc::v1::UnaryRpcRequest* request,
                                ::opensomeip::gateway::grpc::v1::UnaryRpcResponse* response) override;

    ::grpc::Status StreamSomeIpEvents(
        ::grpc::ServerContext* context,
        const ::opensomeip::gateway::grpc::v1::EventSubscribe* request,
        ::grpc::ServerWriter<::opensomeip::gateway::grpc::v1::EventFrame>* writer) override;

private:
    GrpcGateway* owner_;
};

::grpc::Status SomeIpGatewayGrpcService::UnarySomeIp(
    ::grpc::ServerContext* /*context*/,
    const ::opensomeip::gateway::grpc::v1::UnaryRpcRequest* request,
    ::opensomeip::gateway::grpc::v1::UnaryRpcResponse* response) {

    if (!request->has_envelope()) {
        return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT, "missing envelope");
    }

    someip::Message req_msg;
    if (!owner_->translator().envelope_to_message(request->envelope(), req_msg)) {
        return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT, "invalid SOME/IP envelope");
    }

    if (owner_->config().enable_e2e_bridge && owner_->e2e_protection_) {
        auto vr = owner_->e2e_protection_->validate(req_msg, owner_->config().e2e_config);
        if (vr != someip::Result::SUCCESS) {
            return GrpcTranslator::return_code_to_grpc_status(someip::ReturnCode::E_E2E,
                                                              "E2E validation failed");
        }
    }

    if (!request->cloud_target_name().empty()) {
        auto channel = owner_->channel_for_target(request->cloud_target_name());
        if (!channel) {
            return ::grpc::Status(::grpc::StatusCode::NOT_FOUND, "unknown cloud_target_name");
        }
        ::opensomeip::gateway::grpc::v1::SomeIpGateway::Stub stub(channel);
        ::grpc::ClientContext cctx;
        ::opensomeip::gateway::grpc::v1::UnaryRpcRequest fwd = *request;
        fwd.clear_cloud_target_name();
        return stub.UnarySomeIp(&cctx, fwd, response);
    }

    {
        std::lock_guard<std::mutex> lk(owner_->vehicle_servers_mu_);
        auto it = owner_->vehicle_rpc_servers_.find(req_msg.get_service_id());
        if (it != owner_->vehicle_rpc_servers_.end() && it->second != nullptr) {
            if (!it->second->is_method_registered(req_msg.get_method_id())) {
                return GrpcTranslator::return_code_to_grpc_status(someip::ReturnCode::E_UNKNOWN_METHOD);
            }
        }
    }

    if (!req_msg.is_request()) {
        return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT, "envelope must be a SOME/IP request");
    }

    someip::rpc::RpcTimeout tmo;
    tmo.response_timeout = owner_->config().rpc_response_timeout;
    auto sync = owner_->rpc_client().call_method_sync(req_msg.get_service_id(), req_msg.get_method_id(),
                                                      req_msg.get_payload(), tmo);

    someip::Message out;
    someip::MessageId mid(req_msg.get_service_id(), req_msg.get_method_id());
    someip::RequestId rid(req_msg.get_client_id(), req_msg.get_session_id());

    if (sync.result == someip::rpc::RpcResult::SUCCESS) {
        out = someip::Message(mid, rid, someip::MessageType::RESPONSE, someip::ReturnCode::E_OK);
        out.set_payload(sync.return_values);
    } else {
        auto rc = GrpcTranslator::rpc_result_to_return_code(sync.result);
        out = someip::Message(mid, rid, someip::MessageType::ERROR, rc);
    }

    if (owner_->config().enable_e2e_bridge && owner_->e2e_protection_) {
        owner_->e2e_protection_->protect(out, owner_->config().e2e_config);
    }

    owner_->translator().message_to_envelope(out, *response->mutable_envelope());
    owner_->record_someip_to_external(response->envelope().ByteSizeLong());
    return ::grpc::Status::OK;
}

::grpc::Status SomeIpGatewayGrpcService::StreamSomeIpEvents(
    ::grpc::ServerContext* context,
    const ::opensomeip::gateway::grpc::v1::EventSubscribe* request,
    ::grpc::ServerWriter<::opensomeip::gateway::grpc::v1::EventFrame>* writer) {

    EventStreamKey key{request->service_id(), request->instance_id(), request->event_group_id()};
    auto sink = std::make_shared<EventStreamSink>();
    owner_->register_event_stream(key, sink);

    someip::events::EventNotificationCallback on_evt = [this, sink](const someip::events::EventNotification& n) {
        ::opensomeip::gateway::grpc::v1::EventFrame frame;
        someip::Message msg(someip::MessageId{n.service_id, n.event_id},
                            someip::RequestId{n.client_id, n.session_id}, someip::MessageType::NOTIFICATION,
                            someip::ReturnCode::E_OK);
        msg.set_payload(n.event_data);
        owner_->translator().message_to_envelope(msg, *frame.mutable_envelope());
        owner_->deliver_event_frame(sink, frame);
        owner_->record_someip_to_external(frame.envelope().ByteSizeLong());
    };

    bool sub_ok = owner_->event_subscriber().subscribe_eventgroup(
        static_cast<uint16_t>(request->service_id()), static_cast<uint16_t>(request->instance_id()),
        static_cast<uint16_t>(request->event_group_id()), on_evt, nullptr, {});

    if (!sub_ok) {
        owner_->unregister_event_stream(key, sink);
        sink->closed.store(true);
        sink->cv.notify_all();
        return ::grpc::Status(::grpc::StatusCode::FAILED_PRECONDITION,
                              "EventSubscriber::subscribe_eventgroup failed");
    }

    while (!context->IsCancelled()) {
        ::opensomeip::gateway::grpc::v1::EventFrame frame;
        {
            std::unique_lock<std::mutex> lk(sink->mu);
            sink->cv.wait_for(lk, std::chrono::milliseconds(250), [&sink] {
                return sink->closed.load() || !sink->pending.empty();
            });
            if (sink->pending.empty()) {
                if (sink->closed.load()) {
                    break;
                }
                continue;
            }
            frame = std::move(sink->pending.front());
            sink->pending.pop();
        }
        if (!writer->Write(frame)) {
            break;
        }
    }

    owner_->event_subscriber().unsubscribe_eventgroup(static_cast<uint16_t>(request->service_id()),
                                                      static_cast<uint16_t>(request->instance_id()),
                                                      static_cast<uint16_t>(request->event_group_id()));
    owner_->unregister_event_stream(key, sink);
    sink->closed.store(true);
    sink->cv.notify_all();
    return ::grpc::Status::OK;
}

}  // namespace detail

GrpcGateway::GrpcGateway(const GrpcConfig& config)
    : GatewayBase(config.gateway_name, "grpc"),
      config_(config),
      rpc_client_(std::make_unique<someip::rpc::RpcClient>(config.someip_bridge_client_id)),
      event_subscriber_(std::make_unique<someip::events::EventSubscriber>(config.someip_bridge_client_id)) {}

GrpcGateway::~GrpcGateway() {
    stop();
}

std::string GrpcGateway::read_file_or_empty(const std::string& path) const {
    if (path.empty()) {
        return {};
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::shared_ptr<::grpc::ServerCredentials> GrpcGateway::build_server_credentials() const {
    if (!config_.server_tls.enabled) {
        return ::grpc::InsecureServerCredentials();
    }
    ::grpc::SslServerCredentialsOptions opts;
    opts.pem_root_certs = read_file_or_empty(config_.server_tls.client_ca_pem_path);
    ::grpc::SslServerCredentialsOptions::PemKeyCertPair pair;
    pair.private_key = read_file_or_empty(config_.server_tls.private_key_pem_path);
    pair.cert_chain = read_file_or_empty(config_.server_tls.cert_chain_pem_path);
    opts.pem_key_cert_pairs.push_back(std::move(pair));
    opts.client_certificate_request =
        config_.server_tls.require_client_cert ? GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY
                                               : GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE;
    return ::grpc::SslServerCredentials(opts);
}

std::shared_ptr<::grpc::ChannelCredentials> GrpcGateway::build_channel_credentials(
    const GrpcTlsClientOptions& tls) const {
    if (!tls.enabled) {
        return ::grpc::InsecureChannelCredentials();
    }
    ::grpc::SslCredentialsOptions o;
    o.pem_root_certs = read_file_or_empty(tls.root_ca_pem_path);
    o.pem_cert_chain = read_file_or_empty(tls.client_cert_chain_pem_path);
    o.pem_private_key = read_file_or_empty(tls.client_private_key_pem_path);
    return ::grpc::SslCredentials(o);
}

someip::Result GrpcGateway::start() {
    if (is_running()) {
        return someip::Result::SUCCESS;
    }

    if (!rpc_client_->initialize()) {
        return someip::Result::NOT_INITIALIZED;
    }
    if (!event_subscriber_->initialize()) {
        return someip::Result::NOT_INITIALIZED;
    }

    if (config_.enable_sd_client) {
        sd_client_ = std::make_unique<someip::sd::SdClient>(config_.sd_client_config);
        if (!sd_client_->initialize()) {
            return someip::Result::NOT_INITIALIZED;
        }
    }
    if (config_.enable_sd_server) {
        sd_server_ = std::make_unique<someip::sd::SdServer>(config_.sd_server_config);
        if (!sd_server_->initialize()) {
            return someip::Result::NOT_INITIALIZED;
        }
    }
    if (config_.enable_e2e_bridge) {
        e2e_protection_ = std::make_unique<someip::e2e::E2EProtection>();
    }

    grpc_service_ = std::make_unique<detail::SomeIpGatewayGrpcService>(this);
    ::grpc::ServerBuilder builder;
    ::grpc::ResourceQuota quota("opensomeip_grpc_gateway");
    quota.SetMaxOutstandingRpcs(config_.max_concurrent_streams);
    builder.SetResourceQuota(quota);

    int selected_port = 0;
    builder.AddListeningPort(config_.server_listen_address, build_server_credentials(), &selected_port);
    builder.RegisterService(grpc_service_.get());
    grpc_server_ = builder.BuildAndStart();
    if (!grpc_server_ || selected_port == 0) {
        return someip::Result::INTERNAL_ERROR;
    }

    set_running(true);
    return someip::Result::SUCCESS;
}

someip::Result GrpcGateway::stop() {
    if (!is_running()) {
        return someip::Result::SUCCESS;
    }

    if (grpc_server_) {
        grpc_server_->Shutdown(std::chrono::system_clock::now());
        grpc_server_.reset();
    }
    grpc_service_.reset();

    if (sd_client_) {
        sd_client_->shutdown();
        sd_client_.reset();
    }
    if (sd_server_) {
        sd_server_->shutdown();
        sd_server_.reset();
    }

    event_subscriber_->shutdown();
    rpc_client_->shutdown();
    e2e_protection_.reset();

    {
        std::lock_guard<std::mutex> lk(channels_mu_);
        channels_.clear();
    }
    {
        std::lock_guard<std::mutex> lk(stream_mu_);
        stream_subscribers_.clear();
    }

    set_running(false);
    return someip::Result::SUCCESS;
}

someip::Result GrpcGateway::on_someip_message(const someip::Message& msg) {
    if (msg.get_message_type() == someip::MessageType::NOTIFICATION) {
        uint16_t inst = 0;
        const ServiceMapping* mp = find_mapping_for_service(msg.get_service_id(), 0);
        if (mp != nullptr) {
            inst = mp->someip_instance_id;
        }
        ::opensomeip::gateway::grpc::v1::EventFrame frame;
        translator_.message_to_envelope(msg, *frame.mutable_envelope());
        broadcast_notification(msg.get_service_id(), inst, frame);
        record_someip_to_external(frame.envelope().ByteSizeLong());
        return someip::Result::SUCCESS;
    }
    record_external_to_someip(msg.get_total_size());
    return someip::Result::SUCCESS;
}

void GrpcGateway::register_vehicle_rpc_server(uint16_t service_id, someip::rpc::RpcServer& server) {
    std::lock_guard<std::mutex> lk(vehicle_servers_mu_);
    vehicle_rpc_servers_[service_id] = &server;
}

std::shared_ptr<::grpc::Channel> GrpcGateway::channel_for_target(const std::string& target_name) const {
    std::lock_guard<std::mutex> lk(channels_mu_);
    auto it = channels_.find(target_name);
    if (it != channels_.end()) {
        return it->second;
    }
    for (const auto& t : config_.cloud_targets) {
        if (t.name == target_name) {
            auto creds = build_channel_credentials(t.tls);
            ::grpc::ChannelArguments args = config_.default_channel_args;
            if (!t.tls.target_authority_override.empty()) {
                args.SetString(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG, t.tls.target_authority_override);
            }
            auto ch = ::grpc::CreateCustomChannel(t.grpc_uri, creds, args);
            channels_[target_name] = ch;
            return ch;
        }
    }
    return nullptr;
}

void GrpcGateway::register_event_stream(const EventStreamKey& key,
                                        const std::shared_ptr<EventStreamSink>& sink) {
    std::lock_guard<std::mutex> lk(stream_mu_);
    stream_subscribers_.emplace(key, sink);
}

void GrpcGateway::unregister_event_stream(const EventStreamKey& key,
                                          const std::shared_ptr<EventStreamSink>& sink) {
    std::lock_guard<std::mutex> lk(stream_mu_);
    for (auto it = stream_subscribers_.lower_bound(key);
         it != stream_subscribers_.end() && it->first.service_id == key.service_id &&
         it->first.instance_id == key.instance_id && it->first.event_group_id == key.event_group_id;) {
        auto cur = it++;
        if (auto sp = cur->second.lock()) {
            if (sp.get() == sink.get()) {
                stream_subscribers_.erase(cur);
            }
        } else {
            stream_subscribers_.erase(cur);
        }
    }
}

void GrpcGateway::deliver_event_frame(const std::shared_ptr<EventStreamSink>& sink,
                                      const ::opensomeip::gateway::grpc::v1::EventFrame& frame) {
    if (!sink) {
        return;
    }
    std::lock_guard<std::mutex> lk(sink->mu);
    if (sink->closed.load()) {
        return;
    }
    sink->pending.push(frame);
    sink->cv.notify_one();
}

void GrpcGateway::broadcast_notification(uint32_t service_id, uint32_t instance_id,
                                         const ::opensomeip::gateway::grpc::v1::EventFrame& frame) {
    std::lock_guard<std::mutex> lk(stream_mu_);
    for (auto it = stream_subscribers_.begin(); it != stream_subscribers_.end(); ++it) {
        if (it->first.service_id != service_id) {
            continue;
        }
        if (it->first.instance_id != instance_id) {
            continue;
        }
        if (auto sp = it->second.lock()) {
            deliver_event_frame(sp, frame);
        }
    }
}

}  // namespace grpc
}  // namespace gateway
}  // namespace opensomeip
