/*
 * Lightweight diagnostics for the Skree split keyboard.
 *
 * This deliberately avoids per-key log messages. Key transitions are counted
 * with atomics and summarized every five seconds so logging does not materially
 * change keyboard timing.
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>

LOG_MODULE_REGISTER(skree_diag, LOG_LEVEL_INF);

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#define SKREE_DIAG_SIDE "left"
#else
#define SKREE_DIAG_SIDE "right"
#endif

#define HEARTBEAT_INTERVAL K_SECONDS(5)

static atomic_t local_key_events;
static atomic_t remote_key_events;
static atomic_t last_local_key_ms;
static atomic_t last_remote_key_ms;

struct connection_counts {
    uint8_t split;
    uint8_t host;
};

static const char *link_name(uint8_t role) {
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    return role == BT_CONN_ROLE_CENTRAL ? "split" : "host";
#else
    ARG_UNUSED(role);
    return "split";
#endif
}

static const char *role_name(uint8_t role) {
    return role == BT_CONN_ROLE_CENTRAL ? "central" : "peripheral";
}

static void count_connection(struct bt_conn *conn, void *user_data) {
    struct connection_counts *counts = user_data;
    struct bt_conn_info info;

    if (bt_conn_get_info(conn, &info) != 0 || info.type != BT_CONN_TYPE_LE) {
        return;
    }

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    if (info.role == BT_CONN_ROLE_CENTRAL) {
        counts->split++;
    } else {
        counts->host++;
    }
#else
    counts->split++;
#endif
}

static void heartbeat_handler(struct k_work *work) {
    struct connection_counts counts = {0};

    ARG_UNUSED(work);
    bt_conn_foreach(BT_CONN_TYPE_LE, count_connection, &counts);

    LOG_INF("HEARTBEAT side=%s uptime_ms=%u split_links=%u host_links=%u "
            "local_events=%u remote_events=%u last_local_ms=%u last_remote_ms=%u",
            SKREE_DIAG_SIDE, k_uptime_get_32(), counts.split, counts.host,
            (uint32_t)atomic_get(&local_key_events),
            (uint32_t)atomic_get(&remote_key_events),
            (uint32_t)atomic_get(&last_local_key_ms),
            (uint32_t)atomic_get(&last_remote_key_ms));

    k_work_reschedule(k_work_delayable_from_work(work), HEARTBEAT_INTERVAL);
}

K_WORK_DELAYABLE_DEFINE(heartbeat_work, heartbeat_handler);

static int position_state_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *event = as_zmk_position_state_changed(eh);
    atomic_t *counter;
    atomic_t *last_event_ms;

    if (event == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (event->source == ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL) {
        counter = &local_key_events;
        last_event_ms = &last_local_key_ms;
    } else {
        counter = &remote_key_events;
        last_event_ms = &last_remote_key_ms;
    }

    atomic_inc(counter);
    atomic_set(last_event_ms, (atomic_val_t)k_uptime_get_32());
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(skree_diagnostics, position_state_listener);
ZMK_SUBSCRIPTION(skree_diagnostics, zmk_position_state_changed);

static void diag_connected(struct bt_conn *conn, uint8_t err) {
    struct bt_conn_info info;
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    if (err != 0) {
        LOG_ERR("CONNECT_FAILED side=%s addr=%s reason=0x%02x", SKREE_DIAG_SIDE, addr, err);
        return;
    }

    if (bt_conn_get_info(conn, &info) != 0) {
        LOG_WRN("CONNECTED side=%s addr=%s info=unavailable", SKREE_DIAG_SIDE, addr);
        return;
    }

    LOG_INF("CONNECTED side=%s link=%s role=%s addr=%s interval=%u latency=%u timeout=%u",
            SKREE_DIAG_SIDE, link_name(info.role), role_name(info.role), addr, info.le.interval,
            info.le.latency, info.le.timeout);
}

static void diag_disconnected(struct bt_conn *conn, uint8_t reason) {
    struct bt_conn_info info;
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    if (bt_conn_get_info(conn, &info) == 0) {
        LOG_WRN("DISCONNECTED side=%s link=%s role=%s addr=%s reason=0x%02x",
                SKREE_DIAG_SIDE, link_name(info.role), role_name(info.role), addr, reason);
    } else {
        LOG_WRN("DISCONNECTED side=%s link=unknown addr=%s reason=0x%02x", SKREE_DIAG_SIDE,
                addr, reason);
    }
}

static void diag_le_param_updated(struct bt_conn *conn, uint16_t interval, uint16_t latency,
                                  uint16_t timeout) {
    struct bt_conn_info info;
    const char *link = "unknown";

    if (bt_conn_get_info(conn, &info) == 0) {
        link = link_name(info.role);
    }

    LOG_INF("PARAM_UPDATED side=%s link=%s interval=%u latency=%u timeout=%u", SKREE_DIAG_SIDE,
            link, interval, latency, timeout);
}

static void diag_security_changed(struct bt_conn *conn, bt_security_t level,
                                  enum bt_security_err err) {
    struct bt_conn_info info;
    const char *link = "unknown";

    if (err == BT_SECURITY_ERR_SUCCESS) {
        return;
    }

    if (bt_conn_get_info(conn, &info) == 0) {
        link = link_name(info.role);
    }

    LOG_ERR("SECURITY_FAILED side=%s link=%s level=%u error=%u", SKREE_DIAG_SIDE, link, level,
            err);
}

BT_CONN_CB_DEFINE(skree_diag_conn_callbacks) = {
    .connected = diag_connected,
    .disconnected = diag_disconnected,
    .le_param_updated = diag_le_param_updated,
    .security_changed = diag_security_changed,
};

static int skree_diagnostics_init(void) {
    uint32_t reset_cause = 0;
    int err = hwinfo_get_reset_cause(&reset_cause);

    if (err == 0) {
        LOG_INF("BOOT side=%s reset=0x%08x pin=%u software=%u watchdog=%u lockup=%u wake=%u "
                "debug=%u",
                SKREE_DIAG_SIDE, reset_cause, !!(reset_cause & RESET_PIN),
                !!(reset_cause & RESET_SOFTWARE), !!(reset_cause & RESET_WATCHDOG),
                !!(reset_cause & RESET_CPU_LOCKUP), !!(reset_cause & RESET_LOW_POWER_WAKE),
                !!(reset_cause & RESET_DEBUG));
        hwinfo_clear_reset_cause();
    } else {
        LOG_WRN("BOOT side=%s reset=unavailable error=%d", SKREE_DIAG_SIDE, err);
    }

    k_work_schedule(&heartbeat_work, HEARTBEAT_INTERVAL);
    return 0;
}

SYS_INIT(skree_diagnostics_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
