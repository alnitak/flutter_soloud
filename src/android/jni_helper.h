#ifndef JNI_HELPER_H
#define JNI_HELPER_H

#include <jni.h>
#include <android/log.h>

class JNIHelper {
public:
    static void init(JavaVM* vm);
    static JNIEnv* getEnv();
    static void setContext(jobject context);
    static jobject getContext();
    static jobject getContextFromFlutterEngine(JNIEnv* env, jobject flutter_engine);
    
private:
    static JavaVM* javaVM;
    static jobject globalContext;
};

#endif // JNI_HELPER_H
