// SPDX-License-Identifier: GPL-3.0-or-later
// Adapted from xGRIB 0.4.5, source a5606ce; scoped to Weather Routing.
#pragma once

// Run these operations on a worker: Android Keystore can perform IPC.
// Only authenticated ciphertext and its random IV may be written to settings.
#include <QtAndroidExtras/QAndroidJniEnvironment>
#include <QtAndroidExtras/QAndroidJniObject>
#include <QByteArray>
#include <QString>
#include <stdexcept>

namespace weather_routing::android_credentials {
inline void Check(QAndroidJniEnvironment& env) {
  if (env->ExceptionCheck()) {
    env->ExceptionClear();  // Never print a Java exception containing account data.
    throw std::runtime_error("Android secure password storage is unavailable.");
  }
}

inline QAndroidJniObject Store(QAndroidJniEnvironment& env) {
  auto type = QAndroidJniObject::fromString("AndroidKeyStore");
  auto store = QAndroidJniObject::callStaticObjectMethod("java/security/KeyStore",
      "getInstance", "(Ljava/lang/String;)Ljava/security/KeyStore;", type.object<jstring>());
  Check(env);
  if (!store.isValid()) throw std::runtime_error("Android secure password storage is unavailable.");
  store.callMethod<void>("load", "(Ljava/security/KeyStore$LoadStoreParameter;)V",
                         static_cast<jobject>(nullptr));
  Check(env);
  return store;
}

inline QAndroidJniObject Alias() {
  return QAndroidJniObject::fromString("org.opencpn.weather-routing.copernicus.v1");
}

inline QAndroidJniObject Key(QAndroidJniEnvironment& env, bool create) {
  auto store = Store(env), alias = Alias();
  auto key = store.callObjectMethod("getKey", "(Ljava/lang/String;[C)Ljava/security/Key;",
                                   alias.object<jstring>(), static_cast<jcharArray>(nullptr));
  Check(env);
  if (key.isValid()) return key;
  if (!create) throw std::runtime_error("Saved password cannot be unlocked on this installation. Please enter it again.");
  auto algorithm = QAndroidJniObject::fromString("AES");
  auto provider = QAndroidJniObject::fromString("AndroidKeyStore");
  auto generator = QAndroidJniObject::callStaticObjectMethod("javax/crypto/KeyGenerator",
      "getInstance", "(Ljava/lang/String;Ljava/lang/String;)Ljavax/crypto/KeyGenerator;",
      algorithm.object<jstring>(), provider.object<jstring>());
  Check(env);
  QAndroidJniObject builder("android/security/keystore/KeyGenParameterSpec$Builder",
      "(Ljava/lang/String;I)V", alias.object<jstring>(), jint(3)); // encrypt | decrypt
  Check(env);
  auto stringClass = env->FindClass("java/lang/String");
  Check(env);
  auto modes = env->NewObjectArray(1, stringClass, nullptr);
  Check(env);
  auto padding = env->NewObjectArray(1, stringClass, nullptr);
  Check(env);
  env->DeleteLocalRef(stringClass);
  auto gcm = QAndroidJniObject::fromString("GCM");
  auto noPadding = QAndroidJniObject::fromString("NoPadding");
  env->SetObjectArrayElement(modes, 0, gcm.object());
  env->SetObjectArrayElement(padding, 0, noPadding.object());
  QAndroidJniObject modeArray(modes), paddingArray(padding);
  env->DeleteLocalRef(modes); env->DeleteLocalRef(padding);
  Check(env);
  builder.callObjectMethod("setBlockModes",
      "([Ljava/lang/String;)Landroid/security/keystore/KeyGenParameterSpec$Builder;",
      modeArray.object<jobjectArray>());
  Check(env);
  builder.callObjectMethod("setEncryptionPaddings",
      "([Ljava/lang/String;)Landroid/security/keystore/KeyGenParameterSpec$Builder;",
      paddingArray.object<jobjectArray>());
  Check(env);
  auto spec = builder.callObjectMethod("build", "()Landroid/security/keystore/KeyGenParameterSpec;");
  Check(env);
  generator.callMethod<void>("init", "(Ljava/security/spec/AlgorithmParameterSpec;)V", spec.object());
  Check(env);
  key = generator.callObjectMethod("generateKey", "()Ljavax/crypto/SecretKey;");
  Check(env);
  if (!key.isValid()) throw std::runtime_error("Android could not create a secure password key.");
  return key;
}

inline QAndroidJniObject Bytes(QAndroidJniEnvironment& env, const QByteArray& bytes) {
  auto array = env->NewByteArray(bytes.size());
  Check(env);
  env->SetByteArrayRegion(array, 0, bytes.size(), reinterpret_cast<const jbyte*>(bytes.constData()));
  QAndroidJniObject result(array);
  env->DeleteLocalRef(array);
  Check(env);
  return result;
}

inline QByteArray Bytes(QAndroidJniEnvironment& env, const QAndroidJniObject& array) {
  if (!array.isValid()) throw std::runtime_error("Android secure password operation failed.");
  QByteArray bytes(env->GetArrayLength(array.object<jbyteArray>()), '\0');
  env->GetByteArrayRegion(array.object<jbyteArray>(), 0, bytes.size(), reinterpret_cast<jbyte*>(bytes.data()));
  Check(env);
  return bytes;
}

struct SensitiveBytes {
  QByteArray value;
  ~SensitiveBytes() { value.fill('\0'); }
};

inline QString Transform(const QString& username, const QString& value, bool encrypt) {
  QAndroidJniEnvironment env;
  auto key = Key(env, encrypt);
  auto transformation = QAndroidJniObject::fromString("AES/GCM/NoPadding");
  auto cipher = QAndroidJniObject::callStaticObjectMethod("javax/crypto/Cipher", "getInstance",
      "(Ljava/lang/String;)Ljavax/crypto/Cipher;", transformation.object<jstring>());
  Check(env);
  SensitiveBytes input;
  if (encrypt) {
    cipher.callMethod<void>("init", "(ILjava/security/Key;)V", jint(1), key.object());
    Check(env);
    input.value = value.toUtf8();
  } else {
    const auto parts = value.split('.');
    if (parts.size() != 3 || parts[0] != "v1")
      throw std::runtime_error("Saved password is invalid. Please enter it again.");
    const auto iv = QByteArray::fromBase64(parts[1].toLatin1());
    input.value = QByteArray::fromBase64(parts[2].toLatin1());
    if (iv.size() != 12 || input.value.size() < 16)
      throw std::runtime_error("Saved password is invalid. Please enter it again.");
    auto ivArray = Bytes(env, iv);
    QAndroidJniObject spec("javax/crypto/spec/GCMParameterSpec", "(I[B)V",
                           jint(128), ivArray.object<jbyteArray>());
    Check(env);
    cipher.callMethod<void>("init", "(ILjava/security/Key;Ljava/security/spec/AlgorithmParameterSpec;)V",
                            jint(2), key.object(), spec.object());
    Check(env);
  }
  // Bind the encrypted password to the exact account it belongs to.
  auto account = Bytes(env, username.toUtf8());
  cipher.callMethod<void>("updateAAD", "([B)V", account.object<jbyteArray>());
  Check(env);
  auto inputArray = Bytes(env, input.value);
  auto outputArray = cipher.callObjectMethod("doFinal", "([B)[B", inputArray.object<jbyteArray>());
  const bool failed = env->ExceptionCheck();
  if (failed) env->ExceptionClear();
  // Erase the Java input array as well as the native UTF-8 copy.
  input.value.fill('\0');
  env->SetByteArrayRegion(inputArray.object<jbyteArray>(), 0, input.value.size(),
                         reinterpret_cast<const jbyte*>(input.value.constData()));
  Check(env);
  if (failed) throw std::runtime_error("Saved password could not be authenticated. Please enter it again.");
  SensitiveBytes output{Bytes(env, outputArray)};
  if (encrypt) {
    auto ivArray = cipher.callObjectMethod("getIV", "()[B");
    Check(env);
    return "v1." + QString::fromLatin1(Bytes(env, ivArray).toBase64()) + "." +
           QString::fromLatin1(output.value.toBase64());
  }
  const QString password = QString::fromUtf8(output.value);
  output.value.fill('\0');
  env->SetByteArrayRegion(outputArray.object<jbyteArray>(), 0, output.value.size(),
                         reinterpret_cast<const jbyte*>(output.value.constData()));
  Check(env);
  return password;
}

inline QString Forget() {
  QAndroidJniEnvironment env;
  auto store = Store(env), alias = Alias();
  store.callMethod<void>("deleteEntry", "(Ljava/lang/String;)V", alias.object<jstring>());
  Check(env);
  return {};
}
} // namespace weather_routing::android_credentials
