// 노드별 명령 펜딩 풀 — 전송 실패한 명령을 노드별 큐에 보관
//
// 파이썬 비유: dict[node_id] = Queue(maxsize=4)
// ESP32에선 dict 없으니 고정 슬롯 배열 + used 플래그로 구현
#pragma once
#include <Arduino.h>
#include "thread_safe_queue.h"
#include "../net/mqtt_handler.h"
#include "../config.h"

class PendingPool {
public:
    using Sender = bool(*)(const MqttCommand&);

    PendingPool() { portMUX_INITIALIZE(&mux_); }

    // 노드의 펜딩 큐에 명령 추가. 슬롯 부족하면 false.
    bool push(const char* nodeId, const MqttCommand& cmd) {
        portENTER_CRITICAL(&mux_);
        Slot* slot = findOrCreate(nodeId);
        portEXIT_CRITICAL(&mux_);
        if (!slot) return false;
        return slot->queue.push(cmd);
    }

    // 특정 노드의 펜딩 큐를 비우며 전송
    void flush(const char* nodeId, Sender sender) {
        portENTER_CRITICAL(&mux_);
        Slot* slot = findSlot(nodeId);
        portEXIT_CRITICAL(&mux_);
        if (!slot) return;

        MqttCommand cmd;
        while (slot->queue.pop(&cmd)) {
            if (!sender(cmd)) {
                slot->queue.push(cmd);
                break;
            }
        }
        releaseIfEmpty(slot);
    }

    // 모든 노드의 펜딩 큐를 비우며 전송
    void flushAll(Sender sender) {
        for (int i = 0; i < MAX_NODES; i++) {
            portENTER_CRITICAL(&mux_);
            bool hasWork = slots_[i].used && !slots_[i].queue.empty();
            char nodeId[18] = {};
            if (hasWork) strlcpy(nodeId, slots_[i].node_id, sizeof(nodeId));
            portEXIT_CRITICAL(&mux_);

            if (hasWork) flush(nodeId, sender);
        }
    }

private:
    struct Slot {
        ThreadSafeQueue<MqttCommand, CMD_PENDING_PER_NODE> queue;
        char node_id[18];
        bool used = false;
    };

    Slot slots_[MAX_NODES];
    portMUX_TYPE mux_;

    // 호출자가 mux_ 잠근 상태에서 호출
    Slot* findOrCreate(const char* nodeId) {
        for (int i = 0; i < MAX_NODES; i++) {
            if (slots_[i].used && strcmp(slots_[i].node_id, nodeId) == 0)
                return &slots_[i];
        }
        for (int i = 0; i < MAX_NODES; i++) {
            if (!slots_[i].used) {
                slots_[i].used = true;
                strlcpy(slots_[i].node_id, nodeId, sizeof(slots_[i].node_id));
                return &slots_[i];
            }
        }
        return nullptr;
    }

    // 호출자가 mux_ 잠근 상태에서 호출
    Slot* findSlot(const char* nodeId) {
        for (int i = 0; i < MAX_NODES; i++) {
            if (slots_[i].used && strcmp(slots_[i].node_id, nodeId) == 0)
                return &slots_[i];
        }
        return nullptr;
    }

    void releaseIfEmpty(Slot* slot) {
        if (!slot->queue.empty()) return;
        portENTER_CRITICAL(&mux_);
        slot->used = false;
        portEXIT_CRITICAL(&mux_);
    }
};
