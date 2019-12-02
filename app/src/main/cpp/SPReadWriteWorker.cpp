//
// Created by Administrator on 2019/11/25.
//

#include "includes/SPReadWriteWorker.h"

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
        read_thread(nullptr),
        g_vm(vm),
        env(nullptr) {
    _serialPort = new SerialPort(name, baudrate);
//    _serialPort->SetTimeout(0);
    _serialPort->Open();
    if (_serialPort->currendState() == State::OPEN) {
        LOGD("打开串口%s成功", name.c_str());
    } else {
        LOGD("打开串口%s失败", name.c_str());
    }
    write_thread = new std::thread(&SPReadWriteWorker::writeLoop, this);

}

void SPReadWriteWorker::doWork(const std::vector<std::string> &msgs) {
    if (msgs[0] == START_READ) {
        read_thread = new std::thread(&SPReadWriteWorker::readLoop, this);
    } else {
        const std::lock_guard<std::mutex> lock(m_mutex);
        mMessages.push(msgs);
        cv.notify_all();
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
    //这行会报deleted
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
    struct pollfd fds[1] = {};
    fds[0].fd = _serialPort->getFileDescriptor();
    fds[0].events = POLLIN;
    int ret = 0;
    //开始循环
    while (true) {
        ret = poll(fds, 1, read_interval);
        if (ret > 0) {
            if (fds[0].revents & POLLIN) {
                _serialPort->Read(data);
                //执行回调
                auto jArr = StringToJByteArray(env, data);
                env->CallVoidMethod(*jcallback, javaCallbackId, jArr);
                env->DeleteLocalRef(jArr);
            }
        } else {
            if (stopRequested()) {
                break;
            }
        }
    }
    LOGD("读线程终止运行");
    if (jcallback)
        env->DeleteGlobalRef(*jcallback);
    if (g_vm)
        g_vm->DetachCurrentThread();
}

SPReadWriteWorker::~SPReadWriteWorker() {
    LOGD("开始销毁SPReadWriteWorker");
    stop();
    usleep(read_interval + 5000);
    if (write_thread != nullptr && write_thread->joinable())
        write_thread->join();
    if (read_thread != nullptr && read_thread->joinable())
        read_thread->join();
    std::queue<std::vector<char>>().swap(mByteMessages);
    std::queue<std::vector<std::string>>().swap(mMessages);
    write_thread = nullptr;
    read_thread = nullptr;
    _serialPort->Close();
    _serialPort = nullptr;
    g_vm = nullptr;
    jcallback = nullptr;
    env = nullptr;

}

void SPReadWriteWorker::writeMessage(const std::vector<std::string> &messages) {
    for (auto &&c:messages) {
        int len = c.length() / 2;
        auto temp = new char[len];
        HexToBytes(c, temp);
        if (stopRequested()) {
            return;
        }
        _serialPort->Write(temp, len);
        delete[]temp;
    }
}

void SPReadWriteWorker::writeLoop() {
    std::unique_lock<std::mutex> lk(m_mutex);
    while (true) {
        cv.wait(lk,
                [&] { return stopRequested() || !mMessages.empty() || !mByteMessages.empty(); });
        if (stopRequested()) {
            break;
        }
        if (!mMessages.empty()) {
            auto commands = std::move(mMessages.front());
            writeMessage(commands);
            mMessages.pop();
            continue;
        }
        if (!mByteMessages.empty()) {
            auto commands = std::move(mByteMessages.front());
            _serialPort->Write(&commands[0], commands.size());
            mByteMessages.pop();
        }
    }
    LOGD("写线程终止运行");
}

void SPReadWriteWorker::doWork(const std::vector<char> &msg) {
    const std::lock_guard<std::mutex> lock(m_mutex);
    mByteMessages.push(msg);
    cv.notify_all();
}

