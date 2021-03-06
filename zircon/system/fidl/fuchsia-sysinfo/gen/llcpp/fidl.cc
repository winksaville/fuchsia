// WARNING: This file is machine generated by fidlgen.

#include <fuchsia/sysinfo/llcpp/fidl.h>
#include <memory>

namespace llcpp {

namespace fuchsia {
namespace sysinfo {

namespace {

[[maybe_unused]]
constexpr uint64_t kDevice_GetHypervisorResource_Ordinal = 0x3868a16b00000000lu;
[[maybe_unused]]
constexpr uint64_t kDevice_GetHypervisorResource_GenOrdinal = 0x71ec9fedac51e7flu;
extern "C" const fidl_type_t fuchsia_sysinfo_DeviceGetHypervisorResourceRequestTable;
extern "C" const fidl_type_t fuchsia_sysinfo_DeviceGetHypervisorResourceResponseTable;
extern "C" const fidl_type_t v1_fuchsia_sysinfo_DeviceGetHypervisorResourceResponseTable;
[[maybe_unused]]
constexpr uint64_t kDevice_GetBoardName_Ordinal = 0x68768b6d00000000lu;
[[maybe_unused]]
constexpr uint64_t kDevice_GetBoardName_GenOrdinal = 0x3e5b005c54d54d8alu;
extern "C" const fidl_type_t fuchsia_sysinfo_DeviceGetBoardNameRequestTable;
extern "C" const fidl_type_t fuchsia_sysinfo_DeviceGetBoardNameResponseTable;
extern "C" const fidl_type_t v1_fuchsia_sysinfo_DeviceGetBoardNameResponseTable;
[[maybe_unused]]
constexpr uint64_t kDevice_GetInterruptControllerInfo_Ordinal = 0x5f8bb9e400000000lu;
[[maybe_unused]]
constexpr uint64_t kDevice_GetInterruptControllerInfo_GenOrdinal = 0x5d276f3ebc0b70b6lu;
extern "C" const fidl_type_t fuchsia_sysinfo_DeviceGetInterruptControllerInfoRequestTable;
extern "C" const fidl_type_t fuchsia_sysinfo_DeviceGetInterruptControllerInfoResponseTable;
extern "C" const fidl_type_t v1_fuchsia_sysinfo_DeviceGetInterruptControllerInfoResponseTable;

}  // namespace
template <>
Device::ResultOf::GetHypervisorResource_Impl<Device::GetHypervisorResourceResponse>::GetHypervisorResource_Impl(zx::unowned_channel _client_end) {
  constexpr uint32_t _kWriteAllocSize = ::fidl::internal::ClampedMessageSize<GetHypervisorResourceRequest, ::fidl::MessageDirection::kSending>();
  ::fidl::internal::AlignedBuffer<_kWriteAllocSize> _write_bytes_inlined;
  auto& _write_bytes_array = _write_bytes_inlined;
  uint8_t* _write_bytes = _write_bytes_array.view().data();
  memset(_write_bytes, 0, GetHypervisorResourceRequest::PrimarySize);
  ::fidl::BytePart _request_bytes(_write_bytes, _kWriteAllocSize, sizeof(GetHypervisorResourceRequest));
  ::fidl::DecodedMessage<GetHypervisorResourceRequest> _decoded_request(std::move(_request_bytes));
  Super::SetResult(
      Device::InPlace::GetHypervisorResource(std::move(_client_end), Super::response_buffer()));
}

Device::ResultOf::GetHypervisorResource Device::SyncClient::GetHypervisorResource() {
  return ResultOf::GetHypervisorResource(zx::unowned_channel(this->channel_));
}

Device::ResultOf::GetHypervisorResource Device::Call::GetHypervisorResource(zx::unowned_channel _client_end) {
  return ResultOf::GetHypervisorResource(std::move(_client_end));
}

template <>
Device::UnownedResultOf::GetHypervisorResource_Impl<Device::GetHypervisorResourceResponse>::GetHypervisorResource_Impl(zx::unowned_channel _client_end, ::fidl::BytePart _response_buffer) {
  FIDL_ALIGNDECL uint8_t _write_bytes[sizeof(GetHypervisorResourceRequest)] = {};
  ::fidl::BytePart _request_buffer(_write_bytes, sizeof(_write_bytes));
  memset(_request_buffer.data(), 0, GetHypervisorResourceRequest::PrimarySize);
  _request_buffer.set_actual(sizeof(GetHypervisorResourceRequest));
  ::fidl::DecodedMessage<GetHypervisorResourceRequest> _decoded_request(std::move(_request_buffer));
  Super::SetResult(
      Device::InPlace::GetHypervisorResource(std::move(_client_end), std::move(_response_buffer)));
}

Device::UnownedResultOf::GetHypervisorResource Device::SyncClient::GetHypervisorResource(::fidl::BytePart _response_buffer) {
  return UnownedResultOf::GetHypervisorResource(zx::unowned_channel(this->channel_), std::move(_response_buffer));
}

Device::UnownedResultOf::GetHypervisorResource Device::Call::GetHypervisorResource(zx::unowned_channel _client_end, ::fidl::BytePart _response_buffer) {
  return UnownedResultOf::GetHypervisorResource(std::move(_client_end), std::move(_response_buffer));
}

::fidl::DecodeResult<Device::GetHypervisorResourceResponse> Device::InPlace::GetHypervisorResource(zx::unowned_channel _client_end, ::fidl::BytePart response_buffer) {
  constexpr uint32_t _write_num_bytes = sizeof(GetHypervisorResourceRequest);
  ::fidl::internal::AlignedBuffer<_write_num_bytes> _write_bytes;
  ::fidl::BytePart _request_buffer = _write_bytes.view();
  _request_buffer.set_actual(_write_num_bytes);
  ::fidl::DecodedMessage<GetHypervisorResourceRequest> params(std::move(_request_buffer));
  Device::SetTransactionHeaderFor::GetHypervisorResourceRequest(params);
  auto _encode_request_result = ::fidl::Encode(std::move(params));
  if (_encode_request_result.status != ZX_OK) {
    return ::fidl::DecodeResult<Device::GetHypervisorResourceResponse>::FromFailure(
        std::move(_encode_request_result));
  }
  auto _call_result = ::fidl::Call<GetHypervisorResourceRequest, GetHypervisorResourceResponse>(
    std::move(_client_end), std::move(_encode_request_result.message), std::move(response_buffer));
  if (_call_result.status != ZX_OK) {
    return ::fidl::DecodeResult<Device::GetHypervisorResourceResponse>::FromFailure(
        std::move(_call_result));
  }
  return ::fidl::Decode(std::move(_call_result.message));
}

template <>
Device::ResultOf::GetBoardName_Impl<Device::GetBoardNameResponse>::GetBoardName_Impl(zx::unowned_channel _client_end) {
  constexpr uint32_t _kWriteAllocSize = ::fidl::internal::ClampedMessageSize<GetBoardNameRequest, ::fidl::MessageDirection::kSending>();
  ::fidl::internal::AlignedBuffer<_kWriteAllocSize> _write_bytes_inlined;
  auto& _write_bytes_array = _write_bytes_inlined;
  uint8_t* _write_bytes = _write_bytes_array.view().data();
  memset(_write_bytes, 0, GetBoardNameRequest::PrimarySize);
  ::fidl::BytePart _request_bytes(_write_bytes, _kWriteAllocSize, sizeof(GetBoardNameRequest));
  ::fidl::DecodedMessage<GetBoardNameRequest> _decoded_request(std::move(_request_bytes));
  Super::SetResult(
      Device::InPlace::GetBoardName(std::move(_client_end), Super::response_buffer()));
}

Device::ResultOf::GetBoardName Device::SyncClient::GetBoardName() {
  return ResultOf::GetBoardName(zx::unowned_channel(this->channel_));
}

Device::ResultOf::GetBoardName Device::Call::GetBoardName(zx::unowned_channel _client_end) {
  return ResultOf::GetBoardName(std::move(_client_end));
}

template <>
Device::UnownedResultOf::GetBoardName_Impl<Device::GetBoardNameResponse>::GetBoardName_Impl(zx::unowned_channel _client_end, ::fidl::BytePart _response_buffer) {
  FIDL_ALIGNDECL uint8_t _write_bytes[sizeof(GetBoardNameRequest)] = {};
  ::fidl::BytePart _request_buffer(_write_bytes, sizeof(_write_bytes));
  memset(_request_buffer.data(), 0, GetBoardNameRequest::PrimarySize);
  _request_buffer.set_actual(sizeof(GetBoardNameRequest));
  ::fidl::DecodedMessage<GetBoardNameRequest> _decoded_request(std::move(_request_buffer));
  Super::SetResult(
      Device::InPlace::GetBoardName(std::move(_client_end), std::move(_response_buffer)));
}

Device::UnownedResultOf::GetBoardName Device::SyncClient::GetBoardName(::fidl::BytePart _response_buffer) {
  return UnownedResultOf::GetBoardName(zx::unowned_channel(this->channel_), std::move(_response_buffer));
}

Device::UnownedResultOf::GetBoardName Device::Call::GetBoardName(zx::unowned_channel _client_end, ::fidl::BytePart _response_buffer) {
  return UnownedResultOf::GetBoardName(std::move(_client_end), std::move(_response_buffer));
}

::fidl::DecodeResult<Device::GetBoardNameResponse> Device::InPlace::GetBoardName(zx::unowned_channel _client_end, ::fidl::BytePart response_buffer) {
  constexpr uint32_t _write_num_bytes = sizeof(GetBoardNameRequest);
  ::fidl::internal::AlignedBuffer<_write_num_bytes> _write_bytes;
  ::fidl::BytePart _request_buffer = _write_bytes.view();
  _request_buffer.set_actual(_write_num_bytes);
  ::fidl::DecodedMessage<GetBoardNameRequest> params(std::move(_request_buffer));
  Device::SetTransactionHeaderFor::GetBoardNameRequest(params);
  auto _encode_request_result = ::fidl::Encode(std::move(params));
  if (_encode_request_result.status != ZX_OK) {
    return ::fidl::DecodeResult<Device::GetBoardNameResponse>::FromFailure(
        std::move(_encode_request_result));
  }
  auto _call_result = ::fidl::Call<GetBoardNameRequest, GetBoardNameResponse>(
    std::move(_client_end), std::move(_encode_request_result.message), std::move(response_buffer));
  if (_call_result.status != ZX_OK) {
    return ::fidl::DecodeResult<Device::GetBoardNameResponse>::FromFailure(
        std::move(_call_result));
  }
  return ::fidl::Decode(std::move(_call_result.message));
}

template <>
Device::ResultOf::GetInterruptControllerInfo_Impl<Device::GetInterruptControllerInfoResponse>::GetInterruptControllerInfo_Impl(zx::unowned_channel _client_end) {
  constexpr uint32_t _kWriteAllocSize = ::fidl::internal::ClampedMessageSize<GetInterruptControllerInfoRequest, ::fidl::MessageDirection::kSending>();
  ::fidl::internal::AlignedBuffer<_kWriteAllocSize> _write_bytes_inlined;
  auto& _write_bytes_array = _write_bytes_inlined;
  uint8_t* _write_bytes = _write_bytes_array.view().data();
  memset(_write_bytes, 0, GetInterruptControllerInfoRequest::PrimarySize);
  ::fidl::BytePart _request_bytes(_write_bytes, _kWriteAllocSize, sizeof(GetInterruptControllerInfoRequest));
  ::fidl::DecodedMessage<GetInterruptControllerInfoRequest> _decoded_request(std::move(_request_bytes));
  Super::SetResult(
      Device::InPlace::GetInterruptControllerInfo(std::move(_client_end), Super::response_buffer()));
}

Device::ResultOf::GetInterruptControllerInfo Device::SyncClient::GetInterruptControllerInfo() {
  return ResultOf::GetInterruptControllerInfo(zx::unowned_channel(this->channel_));
}

Device::ResultOf::GetInterruptControllerInfo Device::Call::GetInterruptControllerInfo(zx::unowned_channel _client_end) {
  return ResultOf::GetInterruptControllerInfo(std::move(_client_end));
}

template <>
Device::UnownedResultOf::GetInterruptControllerInfo_Impl<Device::GetInterruptControllerInfoResponse>::GetInterruptControllerInfo_Impl(zx::unowned_channel _client_end, ::fidl::BytePart _response_buffer) {
  FIDL_ALIGNDECL uint8_t _write_bytes[sizeof(GetInterruptControllerInfoRequest)] = {};
  ::fidl::BytePart _request_buffer(_write_bytes, sizeof(_write_bytes));
  memset(_request_buffer.data(), 0, GetInterruptControllerInfoRequest::PrimarySize);
  _request_buffer.set_actual(sizeof(GetInterruptControllerInfoRequest));
  ::fidl::DecodedMessage<GetInterruptControllerInfoRequest> _decoded_request(std::move(_request_buffer));
  Super::SetResult(
      Device::InPlace::GetInterruptControllerInfo(std::move(_client_end), std::move(_response_buffer)));
}

Device::UnownedResultOf::GetInterruptControllerInfo Device::SyncClient::GetInterruptControllerInfo(::fidl::BytePart _response_buffer) {
  return UnownedResultOf::GetInterruptControllerInfo(zx::unowned_channel(this->channel_), std::move(_response_buffer));
}

Device::UnownedResultOf::GetInterruptControllerInfo Device::Call::GetInterruptControllerInfo(zx::unowned_channel _client_end, ::fidl::BytePart _response_buffer) {
  return UnownedResultOf::GetInterruptControllerInfo(std::move(_client_end), std::move(_response_buffer));
}

::fidl::DecodeResult<Device::GetInterruptControllerInfoResponse> Device::InPlace::GetInterruptControllerInfo(zx::unowned_channel _client_end, ::fidl::BytePart response_buffer) {
  constexpr uint32_t _write_num_bytes = sizeof(GetInterruptControllerInfoRequest);
  ::fidl::internal::AlignedBuffer<_write_num_bytes> _write_bytes;
  ::fidl::BytePart _request_buffer = _write_bytes.view();
  _request_buffer.set_actual(_write_num_bytes);
  ::fidl::DecodedMessage<GetInterruptControllerInfoRequest> params(std::move(_request_buffer));
  Device::SetTransactionHeaderFor::GetInterruptControllerInfoRequest(params);
  auto _encode_request_result = ::fidl::Encode(std::move(params));
  if (_encode_request_result.status != ZX_OK) {
    return ::fidl::DecodeResult<Device::GetInterruptControllerInfoResponse>::FromFailure(
        std::move(_encode_request_result));
  }
  auto _call_result = ::fidl::Call<GetInterruptControllerInfoRequest, GetInterruptControllerInfoResponse>(
    std::move(_client_end), std::move(_encode_request_result.message), std::move(response_buffer));
  if (_call_result.status != ZX_OK) {
    return ::fidl::DecodeResult<Device::GetInterruptControllerInfoResponse>::FromFailure(
        std::move(_call_result));
  }
  return ::fidl::Decode(std::move(_call_result.message));
}


bool Device::TryDispatch(Interface* impl, fidl_msg_t* msg, ::fidl::Transaction* txn) {
  if (msg->num_bytes < sizeof(fidl_message_header_t)) {
    zx_handle_close_many(msg->handles, msg->num_handles);
    txn->Close(ZX_ERR_INVALID_ARGS);
    return true;
  }
  fidl_message_header_t* hdr = reinterpret_cast<fidl_message_header_t*>(msg->bytes);
  switch (hdr->ordinal) {
    case kDevice_GetHypervisorResource_Ordinal:
    case kDevice_GetHypervisorResource_GenOrdinal:
    {
      auto result = ::fidl::DecodeAs<GetHypervisorResourceRequest>(msg);
      if (result.status != ZX_OK) {
        txn->Close(ZX_ERR_INVALID_ARGS);
        return true;
      }
      impl->GetHypervisorResource(
          Interface::GetHypervisorResourceCompleter::Sync(txn));
      return true;
    }
    case kDevice_GetBoardName_Ordinal:
    case kDevice_GetBoardName_GenOrdinal:
    {
      auto result = ::fidl::DecodeAs<GetBoardNameRequest>(msg);
      if (result.status != ZX_OK) {
        txn->Close(ZX_ERR_INVALID_ARGS);
        return true;
      }
      impl->GetBoardName(
          Interface::GetBoardNameCompleter::Sync(txn));
      return true;
    }
    case kDevice_GetInterruptControllerInfo_Ordinal:
    case kDevice_GetInterruptControllerInfo_GenOrdinal:
    {
      auto result = ::fidl::DecodeAs<GetInterruptControllerInfoRequest>(msg);
      if (result.status != ZX_OK) {
        txn->Close(ZX_ERR_INVALID_ARGS);
        return true;
      }
      impl->GetInterruptControllerInfo(
          Interface::GetInterruptControllerInfoCompleter::Sync(txn));
      return true;
    }
    default: {
      return false;
    }
  }
}

bool Device::Dispatch(Interface* impl, fidl_msg_t* msg, ::fidl::Transaction* txn) {
  bool found = TryDispatch(impl, msg, txn);
  if (!found) {
    zx_handle_close_many(msg->handles, msg->num_handles);
    txn->Close(ZX_ERR_NOT_SUPPORTED);
  }
  return found;
}


void Device::Interface::GetHypervisorResourceCompleterBase::Reply(int32_t status, ::zx::resource resource) {
  constexpr uint32_t _kWriteAllocSize = ::fidl::internal::ClampedMessageSize<GetHypervisorResourceResponse, ::fidl::MessageDirection::kSending>();
  FIDL_ALIGNDECL uint8_t _write_bytes[_kWriteAllocSize] = {};
  auto& _response = *reinterpret_cast<GetHypervisorResourceResponse*>(_write_bytes);
  Device::SetTransactionHeaderFor::GetHypervisorResourceResponse(
      ::fidl::DecodedMessage<GetHypervisorResourceResponse>(
          ::fidl::BytePart(reinterpret_cast<uint8_t*>(&_response),
              GetHypervisorResourceResponse::PrimarySize,
              GetHypervisorResourceResponse::PrimarySize)));
  _response.status = std::move(status);
  _response.resource = std::move(resource);
  ::fidl::BytePart _response_bytes(_write_bytes, _kWriteAllocSize, sizeof(GetHypervisorResourceResponse));
  CompleterBase::SendReply(::fidl::DecodedMessage<GetHypervisorResourceResponse>(std::move(_response_bytes)));
}

void Device::Interface::GetHypervisorResourceCompleterBase::Reply(::fidl::BytePart _buffer, int32_t status, ::zx::resource resource) {
  if (_buffer.capacity() < GetHypervisorResourceResponse::PrimarySize) {
    CompleterBase::Close(ZX_ERR_INTERNAL);
    return;
  }
  auto& _response = *reinterpret_cast<GetHypervisorResourceResponse*>(_buffer.data());
  Device::SetTransactionHeaderFor::GetHypervisorResourceResponse(
      ::fidl::DecodedMessage<GetHypervisorResourceResponse>(
          ::fidl::BytePart(reinterpret_cast<uint8_t*>(&_response),
              GetHypervisorResourceResponse::PrimarySize,
              GetHypervisorResourceResponse::PrimarySize)));
  _response.status = std::move(status);
  _response.resource = std::move(resource);
  _buffer.set_actual(sizeof(GetHypervisorResourceResponse));
  CompleterBase::SendReply(::fidl::DecodedMessage<GetHypervisorResourceResponse>(std::move(_buffer)));
}

void Device::Interface::GetHypervisorResourceCompleterBase::Reply(::fidl::DecodedMessage<GetHypervisorResourceResponse> params) {
  Device::SetTransactionHeaderFor::GetHypervisorResourceResponse(params);
  CompleterBase::SendReply(std::move(params));
}


void Device::Interface::GetBoardNameCompleterBase::Reply(int32_t status, ::fidl::StringView name) {
  constexpr uint32_t _kWriteAllocSize = ::fidl::internal::ClampedMessageSize<GetBoardNameResponse, ::fidl::MessageDirection::kSending>();
  FIDL_ALIGNDECL uint8_t _write_bytes[_kWriteAllocSize];
  GetBoardNameResponse _response = {};
  Device::SetTransactionHeaderFor::GetBoardNameResponse(
      ::fidl::DecodedMessage<GetBoardNameResponse>(
          ::fidl::BytePart(reinterpret_cast<uint8_t*>(&_response),
              GetBoardNameResponse::PrimarySize,
              GetBoardNameResponse::PrimarySize)));
  _response.status = std::move(status);
  _response.name = std::move(name);
  auto _linearize_result = ::fidl::Linearize(&_response, ::fidl::BytePart(_write_bytes,
                                                                          _kWriteAllocSize));
  if (_linearize_result.status != ZX_OK) {
    CompleterBase::Close(ZX_ERR_INTERNAL);
    return;
  }
  CompleterBase::SendReply(std::move(_linearize_result.message));
}

void Device::Interface::GetBoardNameCompleterBase::Reply(::fidl::BytePart _buffer, int32_t status, ::fidl::StringView name) {
  if (_buffer.capacity() < GetBoardNameResponse::PrimarySize) {
    CompleterBase::Close(ZX_ERR_INTERNAL);
    return;
  }
  GetBoardNameResponse _response = {};
  Device::SetTransactionHeaderFor::GetBoardNameResponse(
      ::fidl::DecodedMessage<GetBoardNameResponse>(
          ::fidl::BytePart(reinterpret_cast<uint8_t*>(&_response),
              GetBoardNameResponse::PrimarySize,
              GetBoardNameResponse::PrimarySize)));
  _response.status = std::move(status);
  _response.name = std::move(name);
  auto _linearize_result = ::fidl::Linearize(&_response, std::move(_buffer));
  if (_linearize_result.status != ZX_OK) {
    CompleterBase::Close(ZX_ERR_INTERNAL);
    return;
  }
  CompleterBase::SendReply(std::move(_linearize_result.message));
}

void Device::Interface::GetBoardNameCompleterBase::Reply(::fidl::DecodedMessage<GetBoardNameResponse> params) {
  Device::SetTransactionHeaderFor::GetBoardNameResponse(params);
  CompleterBase::SendReply(std::move(params));
}


void Device::Interface::GetInterruptControllerInfoCompleterBase::Reply(int32_t status, ::llcpp::fuchsia::sysinfo::InterruptControllerInfo* info) {
  constexpr uint32_t _kWriteAllocSize = ::fidl::internal::ClampedMessageSize<GetInterruptControllerInfoResponse, ::fidl::MessageDirection::kSending>();
  FIDL_ALIGNDECL uint8_t _write_bytes[_kWriteAllocSize];
  GetInterruptControllerInfoResponse _response = {};
  Device::SetTransactionHeaderFor::GetInterruptControllerInfoResponse(
      ::fidl::DecodedMessage<GetInterruptControllerInfoResponse>(
          ::fidl::BytePart(reinterpret_cast<uint8_t*>(&_response),
              GetInterruptControllerInfoResponse::PrimarySize,
              GetInterruptControllerInfoResponse::PrimarySize)));
  _response.status = std::move(status);
  _response.info = std::move(info);
  auto _linearize_result = ::fidl::Linearize(&_response, ::fidl::BytePart(_write_bytes,
                                                                          _kWriteAllocSize));
  if (_linearize_result.status != ZX_OK) {
    CompleterBase::Close(ZX_ERR_INTERNAL);
    return;
  }
  CompleterBase::SendReply(std::move(_linearize_result.message));
}

void Device::Interface::GetInterruptControllerInfoCompleterBase::Reply(::fidl::BytePart _buffer, int32_t status, ::llcpp::fuchsia::sysinfo::InterruptControllerInfo* info) {
  if (_buffer.capacity() < GetInterruptControllerInfoResponse::PrimarySize) {
    CompleterBase::Close(ZX_ERR_INTERNAL);
    return;
  }
  GetInterruptControllerInfoResponse _response = {};
  Device::SetTransactionHeaderFor::GetInterruptControllerInfoResponse(
      ::fidl::DecodedMessage<GetInterruptControllerInfoResponse>(
          ::fidl::BytePart(reinterpret_cast<uint8_t*>(&_response),
              GetInterruptControllerInfoResponse::PrimarySize,
              GetInterruptControllerInfoResponse::PrimarySize)));
  _response.status = std::move(status);
  _response.info = std::move(info);
  auto _linearize_result = ::fidl::Linearize(&_response, std::move(_buffer));
  if (_linearize_result.status != ZX_OK) {
    CompleterBase::Close(ZX_ERR_INTERNAL);
    return;
  }
  CompleterBase::SendReply(std::move(_linearize_result.message));
}

void Device::Interface::GetInterruptControllerInfoCompleterBase::Reply(::fidl::DecodedMessage<GetInterruptControllerInfoResponse> params) {
  Device::SetTransactionHeaderFor::GetInterruptControllerInfoResponse(params);
  CompleterBase::SendReply(std::move(params));
}



void Device::SetTransactionHeaderFor::GetHypervisorResourceRequest(const ::fidl::DecodedMessage<Device::GetHypervisorResourceRequest>& _msg) {
  fidl_init_txn_header(&_msg.message()->_hdr, 0, kDevice_GetHypervisorResource_Ordinal);
}
void Device::SetTransactionHeaderFor::GetHypervisorResourceResponse(const ::fidl::DecodedMessage<Device::GetHypervisorResourceResponse>& _msg) {
  fidl_init_txn_header(&_msg.message()->_hdr, 0, kDevice_GetHypervisorResource_Ordinal);
}

void Device::SetTransactionHeaderFor::GetBoardNameRequest(const ::fidl::DecodedMessage<Device::GetBoardNameRequest>& _msg) {
  fidl_init_txn_header(&_msg.message()->_hdr, 0, kDevice_GetBoardName_Ordinal);
}
void Device::SetTransactionHeaderFor::GetBoardNameResponse(const ::fidl::DecodedMessage<Device::GetBoardNameResponse>& _msg) {
  fidl_init_txn_header(&_msg.message()->_hdr, 0, kDevice_GetBoardName_Ordinal);
}

void Device::SetTransactionHeaderFor::GetInterruptControllerInfoRequest(const ::fidl::DecodedMessage<Device::GetInterruptControllerInfoRequest>& _msg) {
  fidl_init_txn_header(&_msg.message()->_hdr, 0, kDevice_GetInterruptControllerInfo_Ordinal);
}
void Device::SetTransactionHeaderFor::GetInterruptControllerInfoResponse(const ::fidl::DecodedMessage<Device::GetInterruptControllerInfoResponse>& _msg) {
  fidl_init_txn_header(&_msg.message()->_hdr, 0, kDevice_GetInterruptControllerInfo_Ordinal);
}

}  // namespace sysinfo
}  // namespace fuchsia
}  // namespace llcpp
