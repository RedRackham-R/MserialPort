//
// Created by Administrator on 2019/11/14.
//

#include "includes/SPBackgroundService.h"

SPBackgroundService::SPBackgroundService(std::string &name, BaudRate baudRate) :
        _serialPort(SerialPort(name, baudRate)) {
    _service = std::make_unique<PFBackgroundService>([this](std::string msg) {
        _serialPort.Write(msg);
    });
    _serialPort.Open();
}

SPBackgroundService::~SPBackgroundService() {
    _service.reset(nullptr);
    _serialPort.Close();
}

void SPBackgroundService::processMsg(std::string &msg) {
    if (_serialPort.currendState() == State::OPEN && _service != nullptr) {
        _service->processMessage(msg);
    }
}

