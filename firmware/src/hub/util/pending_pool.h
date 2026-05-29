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
#include "../dto/mqtt_command.h"
#include "../config.h"

class PendingPool {
public:
    PendingPool() { portMUX_INITIALIZE(&mux_); }

    // 노드의 펜딩 큐에 명령 추가. 타임스탬프 자동 기록.
    // lock 범위: findOrCreate + queue.push 전체를 보호
    // — findOrCreate 후 lock 풀고 push하면 다른 태스크가 끼어들 수 있음
    bool push(const char* nodeId, const MqttCommand& cmd) {
        MqttCommand stamped = cmd;
        stamped.queued_at = millis();

        portENTER_CRITICAL(&mux_);
        Slot* slot = findOrCreate(nodeId);
        if (!slot) {
            portEXIT_CRITICAL(&mux_);
            return false;
        }
        bool ok = slot->queue.pushUnsafe(stamped);
        portEXIT_CRITICAL(&mux_);
        return ok;
    }

    // 노드의 펜딩 큐 앞에 명령 삽입 (순서 유지용).
    bool pushFront(const char* nodeId, const MqttCommand& cmd) {
        MqttCommand stamped = cmd;
        stamped.queued_at = millis();

        portENTER_CRITICAL(&mux_);
        Slot* slot = findOrCreate(nodeId);
        if (!slot) {
            portEXIT_CRITICAL(&mux_);
            return false;
        }
        bool ok = slot->queue.pushFrontUnsafe(stamped);
        portEXIT_CRITICAL(&mux_);
        return ok;
    }

    // 노드의 펜딩 큐에서 유효한 명령 하나 꺼냄.
    // 만료된 명령은 건너뛰고 폐기.
    // lock 범위: findSlot + popUnsafe + releaseIfEmpty 전체를 보호
    // — push()와 같은 lock을 사용해야 data race 방지
    bool pop(const char* nodeId, MqttCommand* out) {
        portENTER_CRITICAL(&mux_);
        Slot* slot = findSlot(nodeId);
        if (!slot) {
            portEXIT_CRITICAL(&mux_);
            return false;
        }

        uint32_t now = millis();
        MqttCommand cmd;
        while (slot->queue.popUnsafe(&cmd)) {
            if (now - cmd.queued_at < CMD_TTL_MS) {
                portEXIT_CRITICAL(&mux_);
                *out = cmd;
                return true;
            }
            // 만료된 명령 — 로그는 lock 밖에서 찍고 싶지만,
            // 루프 내에서 lock을 풀면 복잡해지니 그냥 skip
        }
        // 큐가 비었으면 슬롯 해제 (이미 lock 안이므로 안전)
        slot->used = false;
        portEXIT_CRITICAL(&mux_);
        return false;
    }

    // 펜딩 명령이 있는 노드 수
    // lock 범위: 루프 전체를 한 번에 보호 (매 iteration lock/unlock 제거)
    int activeSlots() const {
        portENTER_CRITICAL(&mux_);
        int n = 0;
        for (int i = 0; i < MAX_NODES; i++) {
            if (slots_[i].used) n++;
        }
        portEXIT_CRITICAL(&mux_);
        return n;
    }

    // 전체 펜딩 명령 수
    // lock 범위: 루프 전체를 한 번에 보호 (스냅샷 일관성)
    int totalPending() const {
        portENTER_CRITICAL(&mux_);
        int n = 0;
        for (int i = 0; i < MAX_NODES; i++) {
            if (slots_[i].used) n += slots_[i].queue.countUnsafe();
        }
        portEXIT_CRITICAL(&mux_);
        return n;
    }

    // 펜딩 명령이 있는 노드 ID를 하나씩 꺼냄 (순회용)
    bool nextNode(int* from, char* nodeId, size_t len) {
        for (int i = *from; i < MAX_NODES; i++) {
            portENTER_CRITICAL(&mux_);
            bool hasWork = slots_[i].used && !slots_[i].queue.emptyUnsafe();
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
    // mutable: const 메서드(activeSlots, totalPending)에서도 lock 가능
    mutable portMUX_TYPE mux_;

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

    // releaseIfEmpty 삭제 — pop() 안에서 lock 잡은 채로 직접 처리
};
