// 노드별 명령 펜딩 풀 — 전송 실패한 명령을 노드별 큐에 보관
//
// 파이썬 비유: dict[node_id] = Queue(maxsize=4)
// ESP32에선 dict 없으니 고정 슬롯 배열 + used 플래그로 구현
//
// 순수 데이터 구조 — 보관/꺼내기만 담당, 전송 방법은 모름
// TTL: push 시 millis() 기록, pop 시 만료된 명령은 자동 폐기
#pragma once
#include <Arduino.h>
#include "thread_safe_queue.h"
#include "../net/mqtt_handler.h"
#include "../config.h"

class PendingPool {
public:
    PendingPool() { portMUX_INITIALIZE(&mux_); }

    // 노드의 펜딩 큐에 명령 추가. 타임스탬프 자동 기록.
    bool push(const char* nodeId, const MqttCommand& cmd) {
        portENTER_CRITICAL(&mux_);
        Slot* slot = findOrCreate(nodeId);
        portEXIT_CRITICAL(&mux_);
        if (!slot) return false;

        MqttCommand stamped = cmd;
        stamped.queued_at = millis();
        return slot->queue.push(stamped);
    }

    // 노드의 펜딩 큐에서 유효한 명령 하나 꺼냄.
    // 만료된 명령은 건너뛰고 폐기.
    bool pop(const char* nodeId, MqttCommand* out) {
        portENTER_CRITICAL(&mux_);
        Slot* slot = findSlot(nodeId);
        portEXIT_CRITICAL(&mux_);
        if (!slot) return false;

        uint32_t now = millis();
        MqttCommand cmd;
        while (slot->queue.pop(&cmd)) {
            if (now - cmd.queued_at < CMD_TTL_MS) {
                *out = cmd;
                return true;
            }
            Serial.printf(">> expired: %s → %s\n", cmd.type, cmd.target);
        }
        releaseIfEmpty(slot);
        return false;
    }

    // 펜딩 명령이 있는 노드 수
    int activeSlots() const {
        int n = 0;
        for (int i = 0; i < MAX_NODES; i++) {
            portENTER_CRITICAL(&mux_);
            if (slots_[i].used) n++;
            portEXIT_CRITICAL(&mux_);
        }
        return n;
    }

    // 전체 펜딩 명령 수
    int totalPending() const {
        int n = 0;
        for (int i = 0; i < MAX_NODES; i++) {
            portENTER_CRITICAL(&mux_);
            if (slots_[i].used) n += slots_[i].queue.count();
            portEXIT_CRITICAL(&mux_);
        }
        return n;
    }

    // 펜딩 명령이 있는 노드 ID를 하나씩 꺼냄 (순회용)
    bool nextNode(int* from, char* nodeId, size_t len) {
        for (int i = *from; i < MAX_NODES; i++) {
            portENTER_CRITICAL(&mux_);
            bool hasWork = slots_[i].used && !slots_[i].queue.empty();
            if (hasWork) strlcpy(nodeId, slots_[i].node_id, len);
            portEXIT_CRITICAL(&mux_);

            if (hasWork) {
                *from = i + 1;
                return true;
            }
        }
        return false;
    }

private:
    struct Slot {
        ThreadSafeQueue<MqttCommand, CMD_PENDING_PER_NODE> queue;
        char node_id[18];
        bool used = false;
    };

    Slot slots_[MAX_NODES];
    portMUX_TYPE mux_;

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
