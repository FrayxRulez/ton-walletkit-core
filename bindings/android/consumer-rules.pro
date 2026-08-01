#
# Copyright (c) Fela Ameghino 2026
#
# Distributed under the MIT License. (See accompanying file LICENSE or copy at
# https://opensource.org/licenses/MIT)
#
# Applied to the consuming app's R8 run. Everything here is reached by NAME at
# runtime, so renaming it breaks the binding on device while compiling perfectly.

# The native methods are bound by symbol name (Java_org_ton_walletkit_core_Native_*),
# so both the class and its methods must keep the names the .so was built against.
-keepclasseswithmembernames,includedescriptorclasses class org.ton.walletkit.core.Native {
    native <methods>;
}

# The JNI shim resolves these with GetMethodID by name and signature (see
# twk_jni.cpp): onHttpRequest, onSseOpen, onStorageGet, onLog and friends. They
# are called from C++, so R8 also sees them as unused.
-keep class org.ton.walletkit.core.HostBridge {
    <methods>;
}

# The generated DTOs are deserialized by Gson, which maps JSON keys onto field
# names by reflection. Renaming a field silently turns it into null rather than
# failing, which is the worst kind of bug to chase.
-keep class org.ton.walletkit.api.** { *; }
-keepclassmembers class org.ton.walletkit.core.Ton*Descriptor { *; }

# Gson's own requirements, in case the app has no rules for it.
-keepattributes Signature, InnerClasses, EnclosingMethod
-keepattributes RuntimeVisibleAnnotations, RuntimeVisibleParameterAnnotations
-keep class com.google.gson.reflect.TypeToken { *; }
-keep class * extends com.google.gson.reflect.TypeToken
