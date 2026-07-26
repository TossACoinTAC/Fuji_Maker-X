#include "fuji_ble_transport.h"

#include <esp_app_desc.h>
#include <esp_log.h>
#include <esp_random.h>
#include <cJSON.h>
#include <host/ble_att.h>
#include <host/ble_gap.h>
#include <host/ble_gatt.h>
#include <host/ble_hs.h>
#include <host/ble_store.h>
#include <host/ble_uuid.h>
#include <host/util/util.h>
#include <nimble/hci_common.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <os/os_mbuf.h>
#include <services/gap/ble_svc_gap.h>
#include <services/gatt/ble_svc_gatt.h>
#include <store/config/ble_store_config.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

extern "C" void ble_store_config_init(void);

namespace fuji::ble {
namespace {

constexpr char kTag[] = "FujiBle";
constexpr uint16_t kNoConnection = BLE_HS_CONN_HANDLE_NONE;
constexpr int64_t kPairingWindowUs = 120LL * 1000 * 1000;
constexpr std::size_t kMaximumQueuedEvents = 8;
constexpr uint32_t kMaximumAdvertisingBackoffMs = 60000;

const ble_uuid128_t kServiceUuid = BLE_UUID128_INIT(0x22, 0xbc, 0xa5, 0x0d, 0x1a, 0xcb, 0xc0, 0x8c,
                                                    0x20, 0x40, 0x18, 0x0a, 0xf7, 0xcf, 0x57, 0xf1);
const ble_uuid128_t kCommandUuid = BLE_UUID128_INIT(0x20, 0xfc, 0x3f, 0xa7, 0x2f, 0x0e, 0x4d, 0xbc,
                                                    0xa3, 0x40, 0x59, 0x2d, 0x1a, 0x5b, 0x26, 0x43);
const ble_uuid128_t kEventUuid = BLE_UUID128_INIT(0x12, 0xb9, 0x7e, 0xa9, 0x07, 0xa9, 0x57, 0xa7,
                                                  0x15, 0x4a, 0xdd, 0xd0, 0x46, 0xa0, 0x1b, 0xfd);
const ble_uuid128_t kStateUuid = BLE_UUID128_INIT(0xfd, 0xd1, 0xaf, 0xd0, 0x67, 0x5e, 0xe4, 0x5e,
                                                  0xa7, 0x4c, 0x21, 0x87, 0x4b, 0xe9, 0x06, 0x64);

uint16_t command_value_handle = 0;
uint16_t event_value_handle = 0;
uint16_t state_value_handle = 0;

int GattAccess(uint16_t conn_handle, uint16_t attr_handle, ble_gatt_access_ctxt* context, void*) {
    return FujiBleTransport::GetInstance().HandleGattAccess(conn_handle, attr_handle, context);
}

int GapEvent(ble_gap_event* event, void*) {
    return FujiBleTransport::GetInstance().HandleGapEvent(event);
}

const ble_gatt_chr_def kCharacteristics[] = {
    {
        .uuid = &kCommandUuid.u,
        .access_cb = GattAccess,
        .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC | BLE_GATT_CHR_F_WRITE_AUTHEN,
        .val_handle = &command_value_handle,
    },
    {
        .uuid = &kEventUuid.u,
        .access_cb = GattAccess,
        .flags = BLE_GATT_CHR_F_INDICATE | BLE_GATT_CHR_F_NOTIFY_INDICATE_ENC |
                 BLE_GATT_CHR_F_NOTIFY_INDICATE_AUTHEN,
        .val_handle = &event_value_handle,
    },
    {
        .uuid = &kStateUuid.u,
        .access_cb = GattAccess,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ_ENC |
                 BLE_GATT_CHR_F_READ_AUTHEN | BLE_GATT_CHR_F_NOTIFY_INDICATE_ENC |
                 BLE_GATT_CHR_F_NOTIFY_INDICATE_AUTHEN,
        .val_handle = &state_value_handle,
    },
    {0},
};

const ble_gatt_svc_def kServices[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &kServiceUuid.u,
        .characteristics = kCharacteristics,
    },
    {0},
};

uint64_t MonotonicMs() { return static_cast<uint64_t>(esp_timer_get_time() / 1000); }

bool SameAddress(const ble_addr_t& left, const ble_addr_t& right) {
    return left.type == right.type && std::memcmp(left.val, right.val, sizeof(left.val)) == 0;
}

std::string PrintJson(cJSON* root) {
    char* encoded = cJSON_PrintUnformatted(root);
    std::string result = encoded == nullptr ? std::string() : std::string(encoded);
    cJSON_free(encoded);
    return result;
}

}  // namespace

FujiBleTransport& FujiBleTransport::GetInstance() {
    static FujiBleTransport instance;
    return instance;
}

bool FujiBleTransport::Start(Callbacks callbacks) {
    bool expected = false;
    if (!started_.compare_exchange_strong(expected, true)) {
        return true;
    }
    callbacks_ = std::move(callbacks);

    const esp_timer_create_args_t pairing_args = {
        .callback = PairingTimerCallback,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "fuji_ble_pair",
        .skip_unhandled_events = true,
    };
    const esp_timer_create_args_t reconnect_args = {
        .callback = ReconnectTimerCallback,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "fuji_ble_adv",
        .skip_unhandled_events = true,
    };
    const esp_timer_create_args_t metrics_args = {
        .callback = MetricsTimerCallback,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "fuji_ble_metrics",
        .skip_unhandled_events = true,
    };
    if (esp_timer_create(&pairing_args, &pairing_timer_) != ESP_OK ||
        esp_timer_create(&reconnect_args, &reconnect_timer_) != ESP_OK ||
        esp_timer_create(&metrics_args, &metrics_timer_) != ESP_OK) {
        ESP_LOGE(kTag, "failed to create BLE timers");
        started_.store(false);
        return false;
    }

    const int init_result = nimble_port_init();
    if (init_result != ESP_OK) {
        ESP_LOGE(kTag, "nimble_port_init failed: %d", init_result);
        started_.store(false);
        return false;
    }

    ble_hs_cfg.sync_cb = []() { FujiBleTransport::GetInstance().HandleHostSync(); };
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_YESNO;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    if (!RegisterGattService()) {
        started_.store(false);
        return false;
    }
    ble_store_config_init();
    nimble_port_freertos_init(HostTask);
    esp_timer_start_periodic(metrics_timer_, 60LL * 1000 * 1000);
    ESP_LOGI(kTag, "NimBLE transport started; SC-only, numeric comparison, one bond");
    return true;
}

bool FujiBleTransport::RegisterGattService() {
    ble_svc_gap_init();
    ble_svc_gatt_init();
    int result = ble_gatts_count_cfg(kServices);
    if (result == 0) {
        result = ble_gatts_add_svcs(kServices);
    }
    if (result != 0) {
        ESP_LOGE(kTag, "GATT service registration failed: %d", result);
        return false;
    }
    return ble_svc_gap_device_name_set("Fuji") == 0;
}

void FujiBleTransport::HostTask(void*) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void FujiBleTransport::HandleHostSync() {
    uint8_t own_address_type = 0;
    int result = ble_hs_util_ensure_addr(0);
    if (result == 0) {
        result = ble_hs_id_infer_auto(0, &own_address_type);
    }
    if (result != 0) {
        ESP_LOGE(kTag, "BLE address initialization failed: %d", result);
        return;
    }
    own_address_type_.store(own_address_type);
    host_synced_.store(true);
    int bond_count = 0;
    ble_store_util_count(BLE_STORE_OBJ_TYPE_PEER_SEC, &bond_count);
    if (bond_count == 0) {
        EnterPairingMode();
    } else {
        StartAdvertising();
    }
}

bool FujiBleTransport::EnterPairingMode() {
    pairing_mode_.store(true);
    pairing_deadline_us_.store(esp_timer_get_time() + kPairingWindowUs);
    if (pairing_timer_ != nullptr) {
        esp_timer_stop(pairing_timer_);
        esp_timer_start_once(pairing_timer_, kPairingWindowUs);
    }
    if (callbacks_.on_pairing_window_changed) {
        callbacks_.on_pairing_window_changed(true);
    }
    ESP_LOGI(kTag, "pairing window open for 120 seconds");
    if (host_synced_.load()) {
        if (ble_gap_adv_active()) {
            ble_gap_adv_stop();
        }
        const uint16_t connection_handle = connection_handle_.load();
        if (connection_handle != kNoConnection) {
            ble_gap_terminate(connection_handle, BLE_ERR_REM_USER_CONN_TERM);
        } else {
            StartAdvertising();
        }
    }
    return true;
}

bool FujiBleTransport::ConfirmPendingComparison() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!pairing_mode_.load() || !pending_comparison_.load() ||
        comparison_connection_handle_ == kNoConnection) {
        return false;
    }
    ble_sm_io response = {};
    response.action = BLE_SM_IOACT_NUMCMP;
    response.numcmp_accept = 1;
    const int result = ble_sm_inject_io(comparison_connection_handle_, &response);
    ESP_LOGI(kTag, "numeric comparison accepted: %06lu result=%d",
             static_cast<unsigned long>(comparison_number_), result);
    if (result == 0) {
        pending_comparison_.store(false);
        comparison_connection_handle_ = kNoConnection;
        comparison_number_ = 0;
        return true;
    }
    return false;
}

void FujiBleTransport::PairingTimerCallback(void* argument) {
    static_cast<FujiBleTransport*>(argument)->HandlePairingTimeout();
}

void FujiBleTransport::ReconnectTimerCallback(void* argument) {
    static_cast<FujiBleTransport*>(argument)->StartAdvertising();
}

void FujiBleTransport::MetricsTimerCallback(void* argument) {
    static_cast<FujiBleTransport*>(argument)->LogMetrics();
}

void FujiBleTransport::HandlePairingTimeout() {
    pairing_mode_.store(false);
    uint16_t comparison_handle = kNoConnection;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        comparison_handle = comparison_connection_handle_;
        if (pending_comparison_.exchange(false) && comparison_handle != kNoConnection) {
            ble_sm_io response = {};
            response.action = BLE_SM_IOACT_NUMCMP;
            response.numcmp_accept = 0;
            ble_sm_inject_io(comparison_handle, &response);
        }
        comparison_connection_handle_ = kNoConnection;
        comparison_number_ = 0;
    }
    if (callbacks_.on_pairing_window_changed) {
        callbacks_.on_pairing_window_changed(false);
    }
    ESP_LOGI(kTag, "pairing window closed");
    if (ble_gap_adv_active()) {
        ble_gap_adv_stop();
    }
    int bond_count = 0;
    ble_store_util_count(BLE_STORE_OBJ_TYPE_PEER_SEC, &bond_count);
    if (bond_count > 0 && connection_handle_.load() == kNoConnection) {
        StartAdvertising();
    } else if (bond_count == 0 && comparison_handle != kNoConnection) {
        ble_gap_terminate(comparison_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
}

void FujiBleTransport::StartAdvertising() {
    if (!host_synced_.load() || connection_handle_.load() != kNoConnection ||
        ble_gap_adv_active()) {
        return;
    }
    int bond_count = 0;
    ble_store_util_count(BLE_STORE_OBJ_TYPE_PEER_SEC, &bond_count);
    if (!pairing_mode_.load() && bond_count == 0) {
        return;
    }

    ble_hs_adv_fields fields = {};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    const char* name = ble_svc_gap_device_name();
    fields.name = reinterpret_cast<uint8_t*>(const_cast<char*>(name));
    fields.name_len = std::strlen(name);
    fields.name_is_complete = 1;
    fields.uuids128 = const_cast<ble_uuid128_t*>(&kServiceUuid);
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    int result = ble_gap_adv_set_fields(&fields);
    if (result != 0) {
        ESP_LOGE(kTag, "advertising fields failed: %d", result);
        return;
    }

    ble_gap_adv_params parameters = {};
    parameters.conn_mode = BLE_GAP_CONN_MODE_UND;
    parameters.disc_mode = pairing_mode_.load() ? BLE_GAP_DISC_MODE_LTD : BLE_GAP_DISC_MODE_GEN;
    const int32_t duration_ms =
        pairing_mode_.load() ? static_cast<int32_t>(std::max<int64_t>(
                                   1, (pairing_deadline_us_.load() - esp_timer_get_time()) / 1000))
                             : BLE_HS_FOREVER;
    result = ble_gap_adv_start(own_address_type_.load(), nullptr, duration_ms, &parameters,
                               GapEvent, nullptr);
    if (result != 0) {
        ESP_LOGE(kTag, "advertising start failed: %d", result);
    } else {
        ESP_LOGI(kTag, "advertising started mode=%s", pairing_mode_.load() ? "pairing" : "bonded");
    }
}

void FujiBleTransport::ScheduleAdvertisingBackoff() {
    if (reconnect_timer_ == nullptr) {
        return;
    }
    esp_timer_stop(reconnect_timer_);
    const uint32_t delay_ms = advertising_backoff_ms_.load();
    esp_timer_start_once(reconnect_timer_, static_cast<uint64_t>(delay_ms) * 1000);
    advertising_backoff_ms_.store(std::min(delay_ms * 2, kMaximumAdvertisingBackoffMs));
}

int FujiBleTransport::HandleGapEvent(ble_gap_event* event) {
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT: {
            if (event->connect.status != 0) {
                ESP_LOGW(kTag, "connection failed: %d", event->connect.status);
                ScheduleAdvertisingBackoff();
                return 0;
            }
            connection_handle_.store(event->connect.conn_handle);
            if (!pairing_mode_.load() && !PeerIsBonded(event->connect.conn_handle)) {
                ESP_LOGW(kTag, "rejecting unbonded peer outside pairing window");
                ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
                return 0;
            }
            const int result = ble_gap_security_initiate(event->connect.conn_handle);
            ESP_LOGI(kTag, "connection established; security result=%d", result);
            return 0;
        }
        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGW(kTag, "disconnected reason=%d", event->disconnect.reason);
            metrics_.reconnect_count.fetch_add(1);
            ClearConnectionState();
            if (callbacks_.on_disconnect) {
                callbacks_.on_disconnect();
            }
            ScheduleAdvertisingBackoff();
            return 0;
        case BLE_GAP_EVENT_ADV_COMPLETE:
            if (pairing_mode_.load() && esp_timer_get_time() >= pairing_deadline_us_.load()) {
                HandlePairingTimeout();
            } else {
                ScheduleAdvertisingBackoff();
            }
            return 0;
        case BLE_GAP_EVENT_ENC_CHANGE:
            if (event->enc_change.status != 0 ||
                !ConnectionIsAuthenticated(event->enc_change.conn_handle)) {
                ESP_LOGE(kTag, "authenticated encryption failed status=%d",
                         event->enc_change.status);
                ble_gap_terminate(event->enc_change.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
                return 0;
            }
            secure_connection_.store(true);
            advertising_backoff_ms_.store(1000);
            KeepOnlyPeerBond(event->enc_change.conn_handle);
            if (pairing_mode_.exchange(false)) {
                esp_timer_stop(pairing_timer_);
                if (callbacks_.on_pairing_window_changed) {
                    callbacks_.on_pairing_window_changed(false);
                }
            }
            ESP_LOGI(kTag, "secure bonded connection ready");
            return 0;
        case BLE_GAP_EVENT_PASSKEY_ACTION: {
            if (event->passkey.params.action != BLE_SM_IOACT_NUMCMP || !pairing_mode_.load()) {
                ble_sm_io response = {};
                response.action = event->passkey.params.action;
                if (event->passkey.params.action == BLE_SM_IOACT_NUMCMP) {
                    response.numcmp_accept = 0;
                    ble_sm_inject_io(event->passkey.conn_handle, &response);
                }
                return 0;
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                comparison_connection_handle_ = event->passkey.conn_handle;
                comparison_number_ = event->passkey.params.numcmp;
                pending_comparison_.store(true);
            }
            ESP_LOGI(kTag, "numeric comparison awaiting BOOT confirmation: %06lu",
                     static_cast<unsigned long>(event->passkey.params.numcmp));
            if (callbacks_.on_numeric_comparison) {
                callbacks_.on_numeric_comparison(event->passkey.params.numcmp);
            }
            return 0;
        }
        case BLE_GAP_EVENT_REPEAT_PAIRING: {
            if (!pairing_mode_.load()) {
                return BLE_GAP_REPEAT_PAIRING_IGNORE;
            }
            ble_gap_conn_desc description = {};
            if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &description) == 0) {
                ble_store_util_delete_peer(&description.peer_id_addr);
            }
            return BLE_GAP_REPEAT_PAIRING_RETRY;
        }
        case BLE_GAP_EVENT_SUBSCRIBE: {
            std::lock_guard<std::mutex> lock(mutex_);
            if (event->subscribe.attr_handle == event_value_handle) {
                event_subscribed_ = event->subscribe.cur_indicate;
                if (event_subscribed_) {
                    QueueEventLocked(MakeCapabilityReport());
                }
            } else if (event->subscribe.attr_handle == state_value_handle) {
                state_subscribed_ = event->subscribe.cur_notify;
                if (state_subscribed_ && latest_state_json_.has_value()) {
                    pending_state_json_ = latest_state_json_;
                }
            }
        }
            PumpOutgoing();
            return 0;
        case BLE_GAP_EVENT_NOTIFY_TX:
            HandleTxComplete(event->notify_tx.attr_handle, event->notify_tx.indication,
                             event->notify_tx.status);
            return 0;
        case BLE_GAP_EVENT_MTU:
            ESP_LOGI(kTag, "ATT MTU=%u", event->mtu.value);
            return 0;
        default:
            return 0;
    }
}

bool FujiBleTransport::ConnectionIsAuthenticated(uint16_t conn_handle) const {
    ble_gap_conn_desc description = {};
    return ble_gap_conn_find(conn_handle, &description) == 0 && description.sec_state.encrypted &&
           description.sec_state.authenticated && description.sec_state.bonded;
}

bool FujiBleTransport::PeerIsBonded(uint16_t conn_handle) const {
    ble_gap_conn_desc description = {};
    if (ble_gap_conn_find(conn_handle, &description) != 0) {
        return false;
    }
    std::array<ble_addr_t, 1> peers = {};
    int count = 0;
    if (ble_store_util_bonded_peers(peers.data(), &count, peers.size()) != 0) {
        return false;
    }
    return count == 1 && SameAddress(peers[0], description.peer_id_addr);
}

void FujiBleTransport::KeepOnlyPeerBond(uint16_t conn_handle) {
    ble_gap_conn_desc description = {};
    if (ble_gap_conn_find(conn_handle, &description) != 0) {
        return;
    }
    std::array<ble_addr_t, 2> peers = {};
    int count = 0;
    if (ble_store_util_bonded_peers(peers.data(), &count, peers.size()) != 0) {
        return;
    }
    for (int index = 0; index < count; ++index) {
        if (!SameAddress(peers[index], description.peer_id_addr)) {
            ble_store_util_delete_peer(&peers[index]);
        }
    }
}

void FujiBleTransport::ClearConnectionState() {
    std::lock_guard<std::mutex> lock(mutex_);
    connection_handle_.store(kNoConnection);
    secure_connection_.store(false);
    pending_comparison_.store(false);
    comparison_connection_handle_ = kNoConnection;
    comparison_number_ = 0;
    event_subscribed_ = false;
    state_subscribed_ = false;
    inbound_.ResetConnection();
    event_queue_.clear();
    pending_state_json_.reset();
    active_transfer_.reset();
}

int FujiBleTransport::HandleGattAccess(uint16_t conn_handle, uint16_t attr_handle,
                                       ble_gatt_access_ctxt* context) {
    if (!ConnectionIsAuthenticated(conn_handle)) {
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    }
    if (attr_handle == command_value_handle && context->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        const uint16_t length = OS_MBUF_PKTLEN(context->om);
        if (length < protocol::kFrameHeaderSize || length > 517) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        std::vector<uint8_t> frame(length);
        uint16_t copied = 0;
        if (ble_hs_mbuf_to_flat(context->om, frame.data(), frame.size(), &copied) != 0 ||
            copied != length) {
            return BLE_ATT_ERR_UNLIKELY;
        }
        HandleCommandFrame(frame);
        return 0;
    }
    if (attr_handle == state_value_handle && context->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        PublishStateSnapshot();
        std::optional<std::string> snapshot;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            snapshot = latest_state_json_;
        }
        if (!snapshot.has_value()) {
            return BLE_ATT_ERR_UNLIKELY;
        }
        const std::vector<uint8_t> json(snapshot->begin(), snapshot->end());
        std::vector<std::vector<uint8_t>> frames;
        const uint32_t transfer_id = next_transfer_id_.fetch_add(1);
        if (protocol::EncodeFrames(json, 517, transfer_id, &frames) != protocol::ErrorCode::kNone ||
            frames.size() != 1) {
            return BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        return os_mbuf_append(context->om, frames[0].data(), frames[0].size()) == 0
                   ? 0
                   : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

void FujiBleTransport::HandleCommandFrame(const std::vector<uint8_t>& frame) {
    std::optional<ReceivedMessage> completed;
    protocol::ErrorCode error;
    bool reassembly_timeout = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        error = inbound_.AcceptFrame(frame, MonotonicMs(), &completed);
        reassembly_timeout = inbound_.LastErrorWasReassemblyTimeout();
        if (error != protocol::ErrorCode::kNone) {
            metrics_.protocol_errors.fetch_add(1);
            if (reassembly_timeout) {
                metrics_.reassembly_timeouts.fetch_add(1);
            }
        }
    }
    if (error != protocol::ErrorCode::kNone) {
        ESP_LOGW(kTag, "command rejected: %s", protocol::ErrorCodeName(error));
        SendEvent(MakeProtocolError(error));
        return;
    }
    if (completed.has_value() && callbacks_.on_message) {
        callbacks_.on_message(*completed);
    }
}

bool FujiBleTransport::SendEvent(const std::string& json) {
    protocol::Message message;
    if (protocol::ParseAndValidate(json, &message) != protocol::ErrorCode::kNone ||
        message.direction != protocol::Direction::kDeviceToPhone) {
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!secure_connection_.load() || !event_subscribed_ || !QueueEventLocked(json)) {
            return false;
        }
    }
    PumpOutgoing();
    return true;
}

bool FujiBleTransport::QueueEventLocked(std::string json) {
    if (event_queue_.size() >= kMaximumQueuedEvents) {
        metrics_.queue_overflows.fetch_add(1);
        ESP_LOGW(kTag, "event queue overflow");
        return false;
    }
    event_queue_.push_back(std::move(json));
    return true;
}

void FujiBleTransport::PublishStateSnapshot() {
    if (!callbacks_.make_state_snapshot) {
        return;
    }
    std::string snapshot = callbacks_.make_state_snapshot();
    protocol::Message message;
    if (protocol::ParseAndValidate(snapshot, &message) != protocol::ErrorCode::kNone ||
        message.type != protocol::MessageType::kStateSnapshot ||
        message.direction != protocol::Direction::kDeviceToPhone) {
        ESP_LOGE(kTag, "state snapshot callback returned invalid protocol JSON");
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        latest_state_json_ = snapshot;
        if (secure_connection_.load() && state_subscribed_) {
            pending_state_json_ = std::move(snapshot);
        }
    }
    PumpOutgoing();
}

void FujiBleTransport::PumpOutgoing() {
    std::lock_guard<std::mutex> lock(mutex_);
    while (true) {
        const uint16_t connection_handle = connection_handle_.load();
        if (!secure_connection_.load() || connection_handle == kNoConnection) {
            return;
        }
        if (!active_transfer_.has_value()) {
            std::optional<std::string> json;
            OutgoingKind kind = OutgoingKind::kEvent;
            if (event_subscribed_ && !event_queue_.empty()) {
                json = std::move(event_queue_.front());
                event_queue_.pop_front();
            } else if (state_subscribed_ && pending_state_json_.has_value()) {
                kind = OutgoingKind::kState;
                json = std::move(pending_state_json_);
                pending_state_json_.reset();
            }
            if (!json.has_value()) {
                return;
            }
            const std::vector<uint8_t> bytes(json->begin(), json->end());
            std::vector<std::vector<uint8_t>> frames;
            const uint16_t mtu = ble_att_mtu(connection_handle);
            const uint32_t transfer_id = next_transfer_id_.fetch_add(1);
            if (protocol::EncodeFrames(bytes, mtu, transfer_id, &frames) !=
                protocol::ErrorCode::kNone) {
                metrics_.protocol_errors.fetch_add(1);
                continue;
            }
            active_transfer_ = ActiveTransfer{kind, transfer_id, std::move(frames)};
        }
        ActiveTransfer& transfer = *active_transfer_;
        if (transfer.waiting_for_tx || transfer.next_frame >= transfer.frames.size()) {
            return;
        }
        const std::vector<uint8_t>& frame = transfer.frames[transfer.next_frame];
        os_mbuf* packet = ble_hs_mbuf_from_flat(frame.data(), frame.size());
        if (packet == nullptr) {
            metrics_.queue_overflows.fetch_add(1);
            DropActiveTransferLocked();
            continue;
        }
        transfer.waiting_for_tx = true;
        const int result =
            transfer.kind == OutgoingKind::kEvent
                ? ble_gatts_indicate_custom(connection_handle, event_value_handle, packet)
                : ble_gatts_notify_custom(connection_handle, state_value_handle, packet);
        if (result == 0) {
            return;
        }
        ESP_LOGW(kTag, "outgoing frame failed: %d", result);
        transfer.waiting_for_tx = false;
        metrics_.queue_overflows.fetch_add(1);
        DropActiveTransferLocked();
    }
}

void FujiBleTransport::HandleTxComplete(uint16_t attr_handle, bool indication, int status) {
    bool should_pump = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!active_transfer_.has_value()) {
            return;
        }
        ActiveTransfer& transfer = *active_transfer_;
        const bool expected_handle =
            (transfer.kind == OutgoingKind::kEvent && attr_handle == event_value_handle) ||
            (transfer.kind == OutgoingKind::kState && attr_handle == state_value_handle);
        if (!expected_handle) {
            return;
        }
        const bool completed = indication ? status == BLE_HS_EDONE : status == 0;
        if (!completed) {
            if (status != 0) {
                ESP_LOGW(kTag, "outgoing confirmation failed: %d", status);
                DropActiveTransferLocked();
                should_pump = true;
            }
        } else {
            transfer.waiting_for_tx = false;
            ++transfer.next_frame;
            if (transfer.next_frame >= transfer.frames.size()) {
                active_transfer_.reset();
            }
            should_pump = true;
        }
    }
    if (should_pump) {
        PumpOutgoing();
    }
}

void FujiBleTransport::DropActiveTransferLocked() { active_transfer_.reset(); }

std::string FujiBleTransport::MakeCapabilityReport() const {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "version", 1);
    cJSON_AddStringToObject(root, "message_id", MakeMessageId().c_str());
    cJSON_AddStringToObject(root, "direction", "device_to_phone");
    cJSON_AddStringToObject(root, "type", "capability_report");
    cJSON_AddNumberToObject(root, "ttl_ms", 120000);
    cJSON* payload = cJSON_AddObjectToObject(root, "payload");
    cJSON* versions = cJSON_AddArrayToObject(payload, "protocol_versions");
    cJSON_AddItemToArray(versions, cJSON_CreateNumber(1));
    const esp_app_desc_t* description = esp_app_get_description();
    cJSON_AddStringToObject(payload, "firmware_version", description->version);
    char build_id[17] = {};
    for (std::size_t index = 0; index < 8; ++index) {
        std::snprintf(build_id + index * 2, sizeof(build_id) - index * 2, "%02x",
                      description->app_elf_sha256[index]);
    }
    cJSON_AddStringToObject(payload, "build_id", build_id);
    cJSON_AddNumberToObject(payload, "max_payload_bytes", protocol::kMaximumJsonBytes);
    cJSON* capabilities = cJSON_AddArrayToObject(payload, "capabilities");
    for (const char* capability : {"food_search", "start_navigation", "ble_transport_v1"}) {
        cJSON_AddItemToArray(capabilities, cJSON_CreateString(capability));
    }
    std::string json = PrintJson(root);
    cJSON_Delete(root);
    return json;
}

std::string FujiBleTransport::MakeStateSnapshot(const char* device_state, bool earphones_verified,
                                                const char* active_request_id) const {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "version", 1);
    cJSON_AddStringToObject(root, "message_id", MakeMessageId().c_str());
    cJSON_AddStringToObject(root, "direction", "device_to_phone");
    cJSON_AddStringToObject(root, "type", "state_snapshot");
    cJSON_AddNumberToObject(root, "ttl_ms", 30000);
    cJSON* payload = cJSON_AddObjectToObject(root, "payload");
    cJSON_AddStringToObject(payload, "device_state", device_state);
    if (active_request_id != nullptr && active_request_id[0] != '\0') {
        cJSON_AddStringToObject(payload, "active_request_id", active_request_id);
    }
    cJSON_AddBoolToObject(payload, "earphones_verified", earphones_verified);
    cJSON_AddStringToObject(payload, "firmware_version", esp_app_get_description()->version);
    cJSON* capabilities = cJSON_AddArrayToObject(payload, "capabilities");
    for (const char* capability : {"food_search", "start_navigation", "state_snapshot"}) {
        cJSON_AddItemToArray(capabilities, cJSON_CreateString(capability));
    }
    std::string json = PrintJson(root);
    cJSON_Delete(root);
    return json;
}

std::string FujiBleTransport::MakeProtocolError(protocol::ErrorCode error) const {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "version", 1);
    cJSON_AddStringToObject(root, "message_id", MakeMessageId().c_str());
    cJSON_AddStringToObject(root, "direction", "device_to_phone");
    cJSON_AddStringToObject(root, "type", "protocol_error");
    cJSON_AddNumberToObject(root, "ttl_ms", 30000);
    cJSON* payload = cJSON_AddObjectToObject(root, "payload");
    cJSON_AddStringToObject(payload, "error_code", protocol::ErrorCodeName(error));
    cJSON_AddStringToObject(payload, "message", "Phone command rejected by Fuji protocol v1");
    std::string json = PrintJson(root);
    cJSON_Delete(root);
    return json;
}

std::string FujiBleTransport::MakeMessageId() const {
    std::array<uint8_t, 16> bytes = {};
    for (std::size_t index = 0; index < bytes.size(); index += sizeof(uint32_t)) {
        const uint32_t value = esp_random();
        std::memcpy(bytes.data() + index, &value, sizeof(value));
    }
    bytes[6] = static_cast<uint8_t>((bytes[6] & 0x0fU) | 0x40U);
    bytes[8] = static_cast<uint8_t>((bytes[8] & 0x3fU) | 0x80U);
    char uuid[37] = {};
    std::snprintf(uuid, sizeof(uuid),
                  "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", bytes[0],
                  bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7], bytes[8],
                  bytes[9], bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
    return uuid;
}

void FujiBleTransport::LogMetrics() {
    const uint16_t connection_handle = connection_handle_.load();
    if (connection_handle != kNoConnection) {
        int8_t rssi = 0;
        if (ble_gap_conn_rssi(connection_handle, &rssi) == 0) {
            metrics_.last_rssi.store(rssi);
        }
    }
    ESP_LOGI(kTag,
             "metrics rssi=%d reconnects=%lu protocol_errors=%lu reassembly_timeouts=%lu "
             "queue_overflows=%lu",
             metrics_.last_rssi.load(), static_cast<unsigned long>(metrics_.reconnect_count.load()),
             static_cast<unsigned long>(metrics_.protocol_errors.load()),
             static_cast<unsigned long>(metrics_.reassembly_timeouts.load()),
             static_cast<unsigned long>(metrics_.queue_overflows.load()));
}

}  // namespace fuji::ble
