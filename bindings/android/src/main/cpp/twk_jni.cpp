//
// Copyright (c) Fela Ameghino 2026
//
// Distributed under the MIT License. (See accompanying file LICENSE or copy at
// https://opensource.org/licenses/MIT)
//
// JNI shim for ton-walletkit-core.
//
// Everything crosses this boundary as byte[], never jstring: JNI's
// GetStringUTFChars produces *modified* UTF-8, which mangles anything outside
// the BMP — an emoji in a dapp name or a transaction comment would arrive
// corrupted, and silently. Java encodes and decodes with real UTF-8 on its side
// (see WalletKitClient.java).
//
// Threading: the core calls delegates from its own worker thread, which the JVM
// knows nothing about, so each callback attaches as a daemon thread. It stays
// attached — there is one such thread per client, and detaching per call would
// cost more than it saves.
//
#include <jni.h>

#include <cstring>
#include <string>

#include "twk/twk.h"
#include "twk/twk_delegates.h"

namespace {

JavaVM* g_vm = nullptr;

// The Java object the delegates call into, plus the method ids resolved once.
struct HostBridge {
    jobject host = nullptr; // global ref
    jmethodID http_request = nullptr;
    jmethodID http_cancel = nullptr;
    jmethodID sse_open = nullptr;
    jmethodID sse_close = nullptr;
    jmethodID storage_get = nullptr;
    jmethodID storage_set = nullptr;
    jmethodID storage_remove = nullptr;
    jmethodID storage_clear = nullptr;
    jmethodID log = nullptr;
    twk_delegates delegates{};
};

/**
 * Attaches this thread, whatever its jni.h says the out-parameter is.
 *
 * Android declares AttachCurrentThreadAsDaemon taking JNIEnv**, the reference
 * JNI headers take void**, and a cast that satisfies one is an error on the
 * other. Deducing the type from the member function means neither platform
 * needs an #ifdef — and the desktop build cannot compile something Android
 * rejects, which is exactly how this was missed the first time.
 */
template <typename Env>
jint attach_as_daemon(JavaVM* vm, jint (JavaVM::*attach)(Env**, void*), JNIEnv** out) {
    Env* env = nullptr;
    const jint status = (vm->*attach)(&env, nullptr);
    *out = reinterpret_cast<JNIEnv*>(env);
    return status;
}

/** The env for this thread, attaching it to the JVM if it is a native one. */
JNIEnv* env_for_current_thread() {
    if (g_vm == nullptr) {
        return nullptr;
    }

    JNIEnv* env = nullptr;
    const jint status = g_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (status == JNI_OK) {
        return env;
    }
    if (status != JNI_EDETACHED) {
        return nullptr;
    }

    // Daemon, so a still-attached worker cannot keep the JVM alive at shutdown.
    return attach_as_daemon(g_vm, &JavaVM::AttachCurrentThreadAsDaemon, &env) == JNI_OK ? env : nullptr;
}

/** A C string as a Java byte[]; null stays null. */
jbyteArray to_bytes(JNIEnv* env, const char* value) {
    if (value == nullptr) {
        return nullptr;
    }
    const auto length = static_cast<jsize>(std::strlen(value));
    jbyteArray array = env->NewByteArray(length);
    if (array != nullptr && length > 0) {
        env->SetByteArrayRegion(array, 0, length, reinterpret_cast<const jbyte*>(value));
    }
    return array;
}

/** A Java byte[] as a std::string; null becomes empty. */
std::string from_bytes(JNIEnv* env, jbyteArray array) {
    if (array == nullptr) {
        return {};
    }
    const jsize length = env->GetArrayLength(array);
    std::string out(static_cast<size_t>(length), '\0');
    if (length > 0) {
        env->GetByteArrayRegion(array, 0, length, reinterpret_cast<jbyte*>(&out[0]));
    }
    return out;
}

HostBridge* bridge_of(void* user) {
    return static_cast<HostBridge*>(user);
}

// ---- delegates -------------------------------------------------------------
// Each one hands the call to Java and returns immediately; the answer comes back
// through the completion entry points below, from whatever thread Java is on.

void on_http_request(void* user, twk_client* client, twk_token token, const char* method, const char* url,
                     const char* headers_json, const char* body) {
    JNIEnv* env = env_for_current_thread();
    HostBridge* bridge = bridge_of(user);
    if (env == nullptr || bridge == nullptr) {
        twk_http_failed(client, token, "JNI: no host");
        return;
    }

    env->CallVoidMethod(bridge->host, bridge->http_request, reinterpret_cast<jlong>(client),
                        static_cast<jlong>(token), to_bytes(env, method), to_bytes(env, url),
                        to_bytes(env, headers_json), to_bytes(env, body));
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        twk_http_failed(client, token, "JNI: host threw");
    }
}

void on_http_cancel(void* user, twk_client* client, twk_token token) {
    JNIEnv* env = env_for_current_thread();
    HostBridge* bridge = bridge_of(user);
    if (env == nullptr || bridge == nullptr) {
        return;
    }
    env->CallVoidMethod(bridge->host, bridge->http_cancel, reinterpret_cast<jlong>(client),
                        static_cast<jlong>(token));
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
}

void on_sse_open(void* user, twk_client* client, twk_token token, const char* url, const char* headers_json) {
    JNIEnv* env = env_for_current_thread();
    HostBridge* bridge = bridge_of(user);
    if (env == nullptr || bridge == nullptr) {
        twk_sse_closed(client, token, "JNI: no host");
        return;
    }
    env->CallVoidMethod(bridge->host, bridge->sse_open, reinterpret_cast<jlong>(client),
                        static_cast<jlong>(token), to_bytes(env, url), to_bytes(env, headers_json));
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        twk_sse_closed(client, token, "JNI: host threw");
    }
}

void on_sse_close(void* user, twk_client* client, twk_token token) {
    JNIEnv* env = env_for_current_thread();
    HostBridge* bridge = bridge_of(user);
    if (env == nullptr || bridge == nullptr) {
        return;
    }
    env->CallVoidMethod(bridge->host, bridge->sse_close, reinterpret_cast<jlong>(client),
                        static_cast<jlong>(token));
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
}

void on_storage_get(void* user, twk_client* client, twk_token token, const char* key) {
    JNIEnv* env = env_for_current_thread();
    HostBridge* bridge = bridge_of(user);
    if (env == nullptr || bridge == nullptr) {
        twk_storage_respond(client, token, nullptr);
        return;
    }
    env->CallVoidMethod(bridge->host, bridge->storage_get, reinterpret_cast<jlong>(client),
                        static_cast<jlong>(token), to_bytes(env, key));
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        twk_storage_respond(client, token, nullptr);
    }
}

void on_storage_set(void* user, twk_client* client, twk_token token, const char* key, const char* value) {
    JNIEnv* env = env_for_current_thread();
    HostBridge* bridge = bridge_of(user);
    if (env == nullptr || bridge == nullptr) {
        twk_storage_respond(client, token, nullptr);
        return;
    }
    env->CallVoidMethod(bridge->host, bridge->storage_set, reinterpret_cast<jlong>(client),
                        static_cast<jlong>(token), to_bytes(env, key), to_bytes(env, value));
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        twk_storage_respond(client, token, nullptr);
    }
}

void on_storage_remove(void* user, twk_client* client, twk_token token, const char* key) {
    JNIEnv* env = env_for_current_thread();
    HostBridge* bridge = bridge_of(user);
    if (env == nullptr || bridge == nullptr) {
        twk_storage_respond(client, token, nullptr);
        return;
    }
    env->CallVoidMethod(bridge->host, bridge->storage_remove, reinterpret_cast<jlong>(client),
                        static_cast<jlong>(token), to_bytes(env, key));
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        twk_storage_respond(client, token, nullptr);
    }
}

void on_storage_clear(void* user, twk_client* client, twk_token token) {
    JNIEnv* env = env_for_current_thread();
    HostBridge* bridge = bridge_of(user);
    if (env == nullptr || bridge == nullptr) {
        twk_storage_respond(client, token, nullptr);
        return;
    }
    env->CallVoidMethod(bridge->host, bridge->storage_clear, reinterpret_cast<jlong>(client),
                        static_cast<jlong>(token));
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        twk_storage_respond(client, token, nullptr);
    }
}

void on_log(void* user, twk_client* client, int level, const char* message) {
    JNIEnv* env = env_for_current_thread();
    HostBridge* bridge = bridge_of(user);
    if (env == nullptr || bridge == nullptr) {
        return;
    }
    env->CallVoidMethod(bridge->host, bridge->log, reinterpret_cast<jlong>(client), static_cast<jint>(level),
                        to_bytes(env, message));
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
}

} // namespace

extern "C" {

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
    g_vm = vm;
    return JNI_VERSION_1_6;
}

// ---- host bridge -----------------------------------------------------------

JNIEXPORT jlong JNICALL Java_org_ton_walletkit_core_Native_delegatesCreate(JNIEnv* env, jclass, jobject host) {
    auto* bridge = new HostBridge();
    bridge->host = env->NewGlobalRef(host);

    jclass type = env->GetObjectClass(host);
    bridge->http_request = env->GetMethodID(type, "onHttpRequest", "(JJ[B[B[B[B)V");
    bridge->http_cancel = env->GetMethodID(type, "onHttpCancel", "(JJ)V");
    bridge->sse_open = env->GetMethodID(type, "onSseOpen", "(JJ[B[B)V");
    bridge->sse_close = env->GetMethodID(type, "onSseClose", "(JJ)V");
    bridge->storage_get = env->GetMethodID(type, "onStorageGet", "(JJ[B)V");
    bridge->storage_set = env->GetMethodID(type, "onStorageSet", "(JJ[B[B)V");
    bridge->storage_remove = env->GetMethodID(type, "onStorageRemove", "(JJ[B)V");
    bridge->storage_clear = env->GetMethodID(type, "onStorageClear", "(JJ)V");
    bridge->log = env->GetMethodID(type, "onLog", "(JI[B)V");

    bridge->delegates.http_request = on_http_request;
    bridge->delegates.http_cancel = on_http_cancel;
    bridge->delegates.sse_open = on_sse_open;
    bridge->delegates.sse_close = on_sse_close;
    bridge->delegates.storage_get = on_storage_get;
    bridge->delegates.storage_set = on_storage_set;
    bridge->delegates.storage_remove = on_storage_remove;
    bridge->delegates.storage_clear = on_storage_clear;
    bridge->delegates.log = on_log;

    return reinterpret_cast<jlong>(bridge);
}

// Only safe once the client is destroyed: the core may complete a request late,
// and the delegate block must outlive it.
JNIEXPORT void JNICALL Java_org_ton_walletkit_core_Native_delegatesDestroy(JNIEnv* env, jclass, jlong handle) {
    auto* bridge = reinterpret_cast<HostBridge*>(handle);
    if (bridge == nullptr) {
        return;
    }
    if (bridge->host != nullptr) {
        env->DeleteGlobalRef(bridge->host);
    }
    delete bridge;
}

// ---- client ----------------------------------------------------------------

JNIEXPORT jlong JNICALL Java_org_ton_walletkit_core_Native_clientCreate(JNIEnv*, jclass, jlong delegates) {
    // The bridge is the delegates' user pointer: twk_client_create takes it
    // separately, and every callback receives it back.
    auto* bridge = reinterpret_cast<HostBridge*>(delegates);
    twk_client* client = twk_client_create(bridge != nullptr ? &bridge->delegates : nullptr, bridge);
    return reinterpret_cast<jlong>(client);
}

JNIEXPORT void JNICALL Java_org_ton_walletkit_core_Native_clientDestroy(JNIEnv*, jclass, jlong client) {
    twk_client_destroy(reinterpret_cast<twk_client*>(client));
}

JNIEXPORT void JNICALL Java_org_ton_walletkit_core_Native_send(JNIEnv* env, jclass, jlong client,
                                                               jlong request_id, jbyteArray method,
                                                               jbyteArray params) {
    const std::string method_utf8 = from_bytes(env, method);
    const std::string params_utf8 = from_bytes(env, params);
    twk_send(reinterpret_cast<twk_client*>(client), static_cast<unsigned long long>(request_id),
             method_utf8.c_str(), params_utf8.c_str());
}

/**
 * Blocks up to `timeout` seconds. `request_id_out` receives the id (0 for an
 * unsolicited update); null is returned on timeout.
 */
JNIEXPORT jbyteArray JNICALL Java_org_ton_walletkit_core_Native_receive(JNIEnv* env, jclass, jlong client,
                                                                       jdouble timeout,
                                                                       jlongArray request_id_out) {
    // The ABI says unsigned long long, which is not uint64_t on LP64 platforms
    // even though it is the same width — so the declaration follows the header.
    unsigned long long request_id = 0;
    const char* message = twk_receive(reinterpret_cast<twk_client*>(client), timeout, &request_id);
    if (message == nullptr) {
        return nullptr;
    }

    if (request_id_out != nullptr) {
        jlong value = static_cast<jlong>(request_id);
        env->SetLongArrayRegion(request_id_out, 0, 1, &value);
    }
    // Copied here, because the buffer is only valid until the next receive.
    return to_bytes(env, message);
}

// ---- completions -----------------------------------------------------------

JNIEXPORT void JNICALL Java_org_ton_walletkit_core_Native_httpRespond(JNIEnv* env, jclass, jlong client,
                                                                      jlong token, jint status,
                                                                      jbyteArray headers, jbyteArray body) {
    const std::string headers_utf8 = from_bytes(env, headers);
    const std::string body_utf8 = from_bytes(env, body);
    twk_http_respond(reinterpret_cast<twk_client*>(client), static_cast<twk_token>(token),
                     static_cast<int>(status), headers_utf8.c_str(), body_utf8.c_str());
}

JNIEXPORT void JNICALL Java_org_ton_walletkit_core_Native_httpFailed(JNIEnv* env, jclass, jlong client,
                                                                     jlong token, jbyteArray error) {
    const std::string error_utf8 = from_bytes(env, error);
    twk_http_failed(reinterpret_cast<twk_client*>(client), static_cast<twk_token>(token), error_utf8.c_str());
}

JNIEXPORT void JNICALL Java_org_ton_walletkit_core_Native_sseEvent(JNIEnv* env, jclass, jlong client,
                                                                   jlong token, jbyteArray data) {
    const std::string data_utf8 = from_bytes(env, data);
    twk_sse_event(reinterpret_cast<twk_client*>(client), static_cast<twk_token>(token), data_utf8.c_str());
}

JNIEXPORT void JNICALL Java_org_ton_walletkit_core_Native_sseClosed(JNIEnv* env, jclass, jlong client,
                                                                    jlong token, jbyteArray error) {
    if (error == nullptr) {
        twk_sse_closed(reinterpret_cast<twk_client*>(client), static_cast<twk_token>(token), nullptr);
        return;
    }
    const std::string error_utf8 = from_bytes(env, error);
    twk_sse_closed(reinterpret_cast<twk_client*>(client), static_cast<twk_token>(token), error_utf8.c_str());
}

JNIEXPORT void JNICALL Java_org_ton_walletkit_core_Native_storageRespond(JNIEnv* env, jclass, jlong client,
                                                                         jlong token, jbyteArray value) {
    if (value == nullptr) {
        twk_storage_respond(reinterpret_cast<twk_client*>(client), static_cast<twk_token>(token), nullptr);
        return;
    }
    const std::string value_utf8 = from_bytes(env, value);
    twk_storage_respond(reinterpret_cast<twk_client*>(client), static_cast<twk_token>(token),
                        value_utf8.c_str());
}

} // extern "C"
