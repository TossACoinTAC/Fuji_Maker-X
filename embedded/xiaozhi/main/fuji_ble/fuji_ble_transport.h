#pragma once

#include "fuji_ble_session.h"

#include <esp_timer.h>

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct ble_gap_event;
struct ble_gatt_access_ctxt;

namespace fuji::ble {

class FujiBleTransport {
public:
    struct Callbacks {
        std::function<std::string()> make_state_snapshot;
        std::function<void(const ReceivedMessage&)> on_message;
        std::function<void()> on_disconnect;
        std::function<void(bool)> on_pairing_window_changed;
        std::function<void(uint32_t)> on_numeric_comparison;
    };

    static FujiBleTransport& GetInstance();

    bool Start(Callbacks callbacks);
    bool EnterPairingMode();
    bool ConfirmPendingComparison();
    bool IsPairingMode() const { return pairing_mode_.load(); }
    bool HasPendingComparison() const { return pending_comparison_.load(); }
    bool IsSecurelyConnected() const { return secure_connection_.load(); }

    bool SendEvent(const std::string& json);
    void PublishStateSnapshot();
    std::string MakeStateSnapshot(const char* device_state, bool earphones_verified = false,
                                  const char* active_request_id = nullptr) const;

    int HandleGattAccess(uint16_t conn_handle, uint16_t attr_handle, ble_gatt_access_ctxt* context);
    int HandleGapEvent(ble_gap_event* event);
    void HandleHostSync();

private:
    enum class OutgoingKind { kEvent, kState };

    struct ActiveTransfer {
        OutgoingKind kind;
        uint32_t transfer_id;
        std::vector<std::vector<uint8_t>> frames;
        std::size_t next_frame = 0;
        bool waiting_for_tx = false;
    };

    struct Metrics {
        std::atomic<uint32_t> reconnect_count{0};
        std::atomic<uint32_t> protocol_errors{0};
        std::atomic<uint32_t> reassembly_timeouts{0};
        std::atomic<uint32_t> queue_overflows{0};
        std::atomic<int8_t> last_rssi{0};
    };

    FujiBleTransport() = default;
    ~FujiBleTransport() = default;
    FujiBleTransport(const FujiBleTransport&) = delete;
    FujiBleTransport& operator=(const FujiBleTransport&) = delete;

    static void HostTask(void* argument);
    static void PairingTimerCallback(void* argument);
    static void ReconnectTimerCallback(void* argument);
    static void MetricsTimerCallback(void* argument);
    static void SubscriptionReadyTimerCallback(void* argument);
    static void OutgoingTimerCallback(void* argument);

    bool RegisterGattService();
    void StartAdvertising();
    void ScheduleAdvertisingBackoff();
    void HandlePairingTimeout();
    bool ConnectionIsAuthenticated(uint16_t conn_handle) const;
    bool MarkSecureConnectionReady(uint16_t conn_handle);
    bool PeerIsBonded(uint16_t conn_handle) const;
    bool ClearStoredBondsForPairing();
    void KeepOnlyPeerBond(uint16_t conn_handle);
    void ClearConnectionState();
    void HandleCommandFrame(const std::vector<uint8_t>& frame);
    void HandleTxComplete(uint16_t attr_handle, bool indication, int status);
    void PumpOutgoing();
    void DropActiveTransferLocked();
    bool QueueEventLocked(std::string json);
    std::string MakeCapabilityReport() const;
    std::string MakeProtocolError(protocol::ErrorCode error) const;
    std::string MakeMessageId() const;
    void LogMetrics();

    mutable std::mutex mutex_;
    Callbacks callbacks_;
    InboundSession inbound_;
    std::deque<std::string> event_queue_;
    std::optional<std::string> latest_state_json_;
    std::optional<std::string> pending_state_json_;
    std::optional<ActiveTransfer> active_transfer_;
    Metrics metrics_;

    std::atomic<bool> started_{false};
    std::atomic<bool> host_synced_{false};
    std::atomic<bool> pairing_mode_{false};
    std::atomic<bool> pending_comparison_{false};
    std::atomic<bool> secure_connection_{false};
    std::atomic<bool> mtu_ready_{false};
    std::atomic<bool> gatt_cache_invalidation_pending_{false};
    std::atomic<bool> rejecting_existing_bond_{false};
    std::atomic<uint16_t> connection_handle_{0xffff};
    uint16_t comparison_connection_handle_ = 0xffff;
    uint32_t comparison_number_ = 0;
    std::atomic<uint8_t> own_address_type_{0};
    bool event_subscribed_ = false;
    bool state_subscribed_ = false;
    bool outgoing_ready_ = false;
    bool capability_report_queued_ = false;
    bool tx_call_in_progress_ = false;
    std::atomic<uint32_t> next_transfer_id_{1};
    std::atomic<uint32_t> advertising_backoff_ms_{1000};
    std::atomic<int64_t> pairing_deadline_us_{0};
    esp_timer_handle_t pairing_timer_ = nullptr;
    esp_timer_handle_t reconnect_timer_ = nullptr;
    esp_timer_handle_t metrics_timer_ = nullptr;
    esp_timer_handle_t subscription_ready_timer_ = nullptr;
    esp_timer_handle_t outgoing_timer_ = nullptr;
};

}  // namespace fuji::ble
