#
# Copyright (c) Fela Ameghino 2026
#
# Distributed under the MIT License. (See accompanying file LICENSE or copy at
# https://opensource.org/licenses/MIT)
#
# Applied to the consuming app's R8 run. Only what is reached by NAME at runtime
# is kept: renaming any of it breaks the binding on device while compiling
# perfectly.
#
# Nothing about the models is listed here. They are generated to read and write
# themselves field by field, with no reflection anywhere, so R8 is free to
# rename and shrink the whole org.ton.walletkit.api package.

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
