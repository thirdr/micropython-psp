// network: Wi-Fi, in the shape of MicroPython's network.WLAN.
//
//   wlan = network.WLAN()
//   wlan.profiles()             [(1, "Home"), ...]: the XMB's saved connections
//   wlan.connect("Home")        by name or number; waits (timeout=30 s)
//   wlan.isconnected(); wlan.status(); wlan.ifconfig(); wlan.disconnect()
//   wlan.active()               True while the Wi-Fi switch is on
//
// The PSP connects with the profiles saved in its network settings, so
// there's no connect(ssid, key). Sockets come from the socket module
// (ports/unix/modsocket.c over libcglue). The network libraries are loaded
// and started on first use.
#include <string.h>

#include <pspnet_apctl.h>
#include <pspsdk.h>
#include <psputility.h>
#include <pspwlan.h>

#include "py/mperrno.h"
#include "py/mphal.h"
#include "py/runtime.h"
#include "psp_port.h"

#define PROFILES_MAX    (24)

static bool started;

static void net_start(void) {
    if (started) {
        return;
    }
    // Either may already be loaded (by an earlier run); that's fine.
    sceUtilityLoadNetModule(PSP_NET_MODULE_COMMON);
    sceUtilityLoadNetModule(PSP_NET_MODULE_INET);
    int err = pspSdkInetInit();
    if (err < 0) {
        mp_raise_msg_varg(&mp_type_OSError, MP_ERROR_TEXT("can't start the network (0x%08x)"), err);
    }
    started = true;
}

static int apctl_state(void) {
    int state;
    if (sceNetApctlGetState(&state) < 0) {
        return PSP_NET_APCTL_STATE_DISCONNECTED;
    }
    return state;
}

static mp_obj_t info_str(int code) {
    union SceNetApctlInfo info;
    if (sceNetApctlGetInfo(code, &info) < 0) {
        return MP_OBJ_NEW_QSTR(MP_QSTR_);
    }
    // ip, subNetMask, gateway, primaryDns all overlay the start of the union.
    return mp_obj_new_str(info.ip, strnlen(info.ip, sizeof(info.ip)));
}

// The name of saved profile n into out; false if slot n is empty.
static bool profile_name(int n, char *out, size_t len) {
    if (sceUtilityCheckNetParam(n) != 0) {
        return false;
    }
    netData data;
    if (sceUtilityGetNetParam(n, PSP_NETPARAM_NAME, &data) != 0) {
        return false;
    }
    size_t chars = strnlen(data.asString, sizeof(data.asString));
    if (chars >= len) {
        chars = len - 1;
    }
    memcpy(out, data.asString, chars);
    out[chars] = '\0';
    return true;
}

typedef struct {
    mp_obj_base_t base;
} network_wlan_obj_t;

static const mp_obj_type_t network_wlan_type;
static const network_wlan_obj_t wlan_singleton = { { &network_wlan_type } };

static mp_obj_t wlan_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    // WLAN() or WLAN(network.STA_IF): the PSP has one interface.
    mp_arg_check_num(n_args, n_kw, 0, 1, false);
    if (n_args == 1 && mp_obj_get_int(args[0]) != 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("the PSP only has STA_IF"));
    }
    net_start();
    return MP_OBJ_FROM_PTR(&wlan_singleton);
}

// active(): True while the Wi-Fi switch is on. (The PSP turns the radio on
// and off by itself as connections need it.)
static mp_obj_t wlan_active(size_t n_args, const mp_obj_t *args) {
    if (n_args > 1 && !mp_obj_is_true(args[1])) {
        sceNetApctlDisconnect();
    }
    return mp_obj_new_bool(sceWlanGetSwitchState() == 1);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(wlan_active_obj, 1, 2, wlan_active);

static mp_obj_t wlan_profiles(mp_obj_t self_in) {
    (void)self_in;
    mp_obj_t list = mp_obj_new_list(0, NULL);
    char name[128];
    for (int n = 1; n <= PROFILES_MAX; n++) {
        if (profile_name(n, name, sizeof(name))) {
            mp_obj_t pair[2] = { MP_OBJ_NEW_SMALL_INT(n), mp_obj_new_str(name, strlen(name)) };
            mp_obj_list_append(list, mp_obj_new_tuple(2, pair));
        }
    }
    return list;
}
static MP_DEFINE_CONST_FUN_OBJ_1(wlan_profiles_obj, wlan_profiles);

// connect(profile, timeout=30): profile is a saved profile's number or name.
// Waits until the PSP has an IP address.
static mp_obj_t wlan_connect(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_self, ARG_profile, ARG_timeout };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_self, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_profile, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_timeout, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 30} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    int n = -1;
    char name[128];
    if (mp_obj_is_int(args[ARG_profile].u_obj)) {
        n = mp_obj_get_int(args[ARG_profile].u_obj);
        if (n < 1 || n > PROFILES_MAX || !profile_name(n, name, sizeof(name))) {
            mp_raise_ValueError(MP_ERROR_TEXT("no saved network profile with that number"));
        }
    } else {
        const char *want = mp_obj_str_get_str(args[ARG_profile].u_obj);
        for (int k = 1; k <= PROFILES_MAX && n < 0; k++) {
            if (profile_name(k, name, sizeof(name)) && strcmp(name, want) == 0) {
                n = k;
            }
        }
        if (n < 0) {
            mp_raise_ValueError(MP_ERROR_TEXT("no saved network profile with that name"));
        }
    }
    if (sceWlanGetSwitchState() != 1) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("the Wi-Fi switch is off"));
    }
    if (apctl_state() != PSP_NET_APCTL_STATE_DISCONNECTED) {
        sceNetApctlDisconnect();
        for (int i = 0; i < 100 && apctl_state() != PSP_NET_APCTL_STATE_DISCONNECTED; i++) {
            mp_event_wait_ms(20);
        }
    }
    int err = sceNetApctlConnect(n);
    if (err < 0) {
        mp_raise_msg_varg(&mp_type_OSError, MP_ERROR_TEXT("can't connect (0x%08x)"), err);
    }
    mp_uint_t start = mp_hal_ticks_ms();
    int highest = PSP_NET_APCTL_STATE_DISCONNECTED;
    for (;;) {
        int state = apctl_state();
        if (state == PSP_NET_APCTL_STATE_GOT_IP) {
            return mp_const_none;
        }
        if (state > highest) {
            highest = state;
        } else if (state == PSP_NET_APCTL_STATE_DISCONNECTED && highest != PSP_NET_APCTL_STATE_DISCONNECTED) {
            // It started, then dropped: the access point refused us.
            mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("couldn't join the network"));
        }
        if (mp_hal_ticks_ms() - start > (mp_uint_t)args[ARG_timeout].u_int * 1000) {
            sceNetApctlDisconnect();
            mp_raise_OSError(MP_ETIMEDOUT);
        }
        mp_event_wait_ms(50);
    }
}
static MP_DEFINE_CONST_FUN_OBJ_KW(wlan_connect_obj, 2, wlan_connect);

static mp_obj_t wlan_disconnect(mp_obj_t self_in) {
    (void)self_in;
    sceNetApctlDisconnect();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(wlan_disconnect_obj, wlan_disconnect);

static mp_obj_t wlan_isconnected(mp_obj_t self_in) {
    (void)self_in;
    return mp_obj_new_bool(apctl_state() == PSP_NET_APCTL_STATE_GOT_IP);
}
static MP_DEFINE_CONST_FUN_OBJ_1(wlan_isconnected_obj, wlan_isconnected);

// status(): one of the STAT_* constants, the connection's progress.
static mp_obj_t wlan_status(mp_obj_t self_in) {
    (void)self_in;
    return MP_OBJ_NEW_SMALL_INT(apctl_state());
}
static MP_DEFINE_CONST_FUN_OBJ_1(wlan_status_obj, wlan_status);

// ifconfig(): (ip, netmask, gateway, dns) while connected.
static mp_obj_t wlan_ifconfig(mp_obj_t self_in) {
    (void)self_in;
    if (apctl_state() != PSP_NET_APCTL_STATE_GOT_IP) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("not connected"));
    }
    mp_obj_t items[4] = {
        info_str(PSP_NET_APCTL_INFO_IP),
        info_str(PSP_NET_APCTL_INFO_SUBNETMASK),
        info_str(PSP_NET_APCTL_INFO_GATEWAY),
        info_str(PSP_NET_APCTL_INFO_PRIMDNS),
    };
    return mp_obj_new_tuple(4, items);
}
static MP_DEFINE_CONST_FUN_OBJ_1(wlan_ifconfig_obj, wlan_ifconfig);

static const mp_rom_map_elem_t wlan_locals_table[] = {
    { MP_ROM_QSTR(MP_QSTR_active), MP_ROM_PTR(&wlan_active_obj) },
    { MP_ROM_QSTR(MP_QSTR_profiles), MP_ROM_PTR(&wlan_profiles_obj) },
    { MP_ROM_QSTR(MP_QSTR_connect), MP_ROM_PTR(&wlan_connect_obj) },
    { MP_ROM_QSTR(MP_QSTR_disconnect), MP_ROM_PTR(&wlan_disconnect_obj) },
    { MP_ROM_QSTR(MP_QSTR_isconnected), MP_ROM_PTR(&wlan_isconnected_obj) },
    { MP_ROM_QSTR(MP_QSTR_status), MP_ROM_PTR(&wlan_status_obj) },
    { MP_ROM_QSTR(MP_QSTR_ifconfig), MP_ROM_PTR(&wlan_ifconfig_obj) },
};
static MP_DEFINE_CONST_DICT(wlan_locals, wlan_locals_table);

static MP_DEFINE_CONST_OBJ_TYPE(
    network_wlan_type,
    MP_QSTR_WLAN,
    MP_TYPE_FLAG_NONE,
    make_new, wlan_make_new,
    locals_dict, &wlan_locals
    );

// Disconnects, if a script left a connection up. Used by the launcher when a
// script ends.
void psp_network_reset(void) {
    if (started && apctl_state() != PSP_NET_APCTL_STATE_DISCONNECTED) {
        sceNetApctlDisconnect();
    }
}

static const mp_rom_map_elem_t network_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_network) },
    { MP_ROM_QSTR(MP_QSTR_WLAN), MP_ROM_PTR(&network_wlan_type) },
    { MP_ROM_QSTR(MP_QSTR_STA_IF), MP_ROM_INT(0) },
    { MP_ROM_QSTR(MP_QSTR_STAT_IDLE), MP_ROM_INT(PSP_NET_APCTL_STATE_DISCONNECTED) },
    { MP_ROM_QSTR(MP_QSTR_STAT_SCANNING), MP_ROM_INT(PSP_NET_APCTL_STATE_SCANNING) },
    { MP_ROM_QSTR(MP_QSTR_STAT_CONNECTING), MP_ROM_INT(PSP_NET_APCTL_STATE_JOINING) },
    { MP_ROM_QSTR(MP_QSTR_STAT_GETTING_IP), MP_ROM_INT(PSP_NET_APCTL_STATE_GETTING_IP) },
    { MP_ROM_QSTR(MP_QSTR_STAT_GOT_IP), MP_ROM_INT(PSP_NET_APCTL_STATE_GOT_IP) },
    { MP_ROM_QSTR(MP_QSTR_STAT_AUTHENTICATING), MP_ROM_INT(PSP_NET_APCTL_STATE_EAP_AUTH) },
    { MP_ROM_QSTR(MP_QSTR_STAT_KEY_EXCHANGE), MP_ROM_INT(PSP_NET_APCTL_STATE_KEY_EXCHANGE) },
};
static MP_DEFINE_CONST_DICT(network_module_globals, network_module_globals_table);

const mp_obj_module_t network_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&network_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_network, network_module);
