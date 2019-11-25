//
// Created by Administrator on 2019/11/25.
//

#include "include/SPReadWriteWorker.h"

static jbyteArray StringToJByteArray(JNIEnv *env, const std::string &nativeString) {
    jbyteArray arr = env->NewByteArray(nativeString.length());
    env->SetByteArrayRegion(arr, 0, nativeString.length(), (jbyte *) nativeString.c_str());
    return arr;
}

const int BIT16 = 16;

static void HexToBytes(const std::string &hex, char *result) {
    for (unsigned int i = 0; i < hex.length(); i += 2) {
        std::string byteString = hex.substr(i, 2);
        char byte = (char) strtol(byteString.c_str(), nullptr, BIT16);
        *result = byte;
        result++;
    }
}

SPReadWriteWorker::SPReadWriteWorker(std::string &name, const int &baudrate, JavaVM *vm,
                                     jobject *callback) :
        jcallback(callback),
        work_thread(nullptr),
        g_vm(vm),
        env(nullptr) {
    _serialPort = new SerialPort(name, baudrate);
    _serialPort->SetTimeout(0);
    _serialPort->Open();
    if (_serialPort->currendState() == State::OPEN) {
        LOGD("打开读串口%s成功", name.c_str());
    } else {
        LOGD("打开读串口%s失败", name.c_str());
    }

}

void SPReadWriteWorker::doWork(const std::vector<std::string> &msgs) {
    if (msgs[0] == "start") {
        work_thread = new std::thread(&SPReadWriteWorker::readLoop, this);
    } else {
        std::lock_guard<std::mutex> lockGuard(m_mutex);
        for (auto m:msgs) {
            writeMessage(m);
        }
    }
}

void SPReadWriteWorker::readLoop() {
    int getEnvStat = g_vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6);
    if (getEnvStat == JNI_EDETACHED) {
        //如果没有， 主动附加到jvm环境中，获取到env
        if (g_vm->AttachCurrentThread(&env, nullptr) != 0) {
            std::__throw_runtime_error("获取java虚拟机实例失败!");
        }
    }
    //通过强转后的jcallback 获取到要回调的类
    jclass javaClass = env->GetObjectClass(*jcallback);

    if (javaClass == 0) {
        std::__throw_runtime_error("获取java回调类失败!");
    }

    //获取要回调的方法ID
    jmethodID javaCallbackId = env->GetMethodID(javaClass,
                                                "onDataReceived", "([B)V");
    if (javaCallbackId == nullptr) {
        std::__throw_runtime_error("获取java回调方法失败!");
    }

    std::string data;
    int byte_read;
    //开始循环
    while (!stopRequested()) {
        ioctl(_serialPort->getFileDescriptor(), FIONREAD, &byte_read);
        if (byte_read <= 0) {
            usleep(read_interval);
            continue;
        }
        _serialPort->Read(data);
        if (!data.empty()) {
            //执行回调
            if (stopRequested()) {
                break;
            }
            env->CallVoidMethod(*jcallback, javaCallbackId, StringToJByteArray(env, data));
        }
    }
    LOGD("读线程终止运行");
    if (jcallback)
        env->DeleteGlobalRef(*jcallback);
    if (g_vm)
        g_vm->DetachCurrentThread();
}

SPReadWriteWorker::~SPReadWriteWorker() {

}

void SPReadWriteWorker::writeMessage(const std::string &msg) {
    int len = msg.length() / 2;
    char temp[len];
    HexToBytes(msg, temp);
    _serialPort->Write(temp, len);
}

