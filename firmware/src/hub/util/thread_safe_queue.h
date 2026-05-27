// 스레드 안전 링 버퍼 — 태스크 간 데이터 전달용 FIFO 큐
//
// 파이썬의 queue.Queue와 같은 역할이지만, FreeRTOS portMUX로 보호.
// 파이썬에선 GIL + Queue가 알아서 해주지만, ESP32에선 직접 잠금이 필요.
//
// 사용 예:
//   ThreadSafeQueue<SensorReport, 8> reportQueue;
//   reportQueue.push(report);   // 생산자 태스크
//   reportQueue.pop(&out);      // 소비자 태스크
#pragma once
#include <Arduino.h>

// T = 큐에 넣을 타입, N = 최대 크기 (컴파일 타임 상수)
// 파이썬 비유: Queue(maxsize=N) — 꽉 차면 push가 False 리턴 (블로킹 아님)
template <typename T, int N>
class ThreadSafeQueue {
public:
    ThreadSafeQueue() : head_(0), tail_(0), count_(0) {
        // portMUX는 사용 전 반드시 초기화해야 함 (파이썬 Lock()과 비슷)
        portMUX_INITIALIZE(&mux_);
    }

    // 큐에 아이템 추가. 꽉 차면 false.
    // 파이썬 비유: queue.put_nowait(item) — Full이면 False 리턴
    bool push(const T& item) {
        portENTER_CRITICAL(&mux_);
        if (count_ >= N) {
            portEXIT_CRITICAL(&mux_);
            return false;
        }
        buf_[tail_] = item;
        tail_ = (tail_ + 1) % N;
        count_++;
        portEXIT_CRITICAL(&mux_);
        return true;
    }

    // 큐에서 아이템 꺼냄. 비었으면 false.
    // 파이썬 비유: queue.get_nowait() — Empty이면 False 리턴
    bool pop(T* out) {
        portENTER_CRITICAL(&mux_);
        if (count_ <= 0) {
            portEXIT_CRITICAL(&mux_);
            return false;
        }
        *out = buf_[head_];
        head_ = (head_ + 1) % N;
        count_--;
        portEXIT_CRITICAL(&mux_);
        return true;
    }

    // 현재 큐에 들어있는 아이템 수
    int count() const {
        portENTER_CRITICAL(&mux_);
        int c = count_;
        portEXIT_CRITICAL(&mux_);
        return c;
    }

    // 큐가 비었는지
    bool empty() const {
        portENTER_CRITICAL(&mux_);
        bool e = (count_ <= 0);
        portEXIT_CRITICAL(&mux_);
        return e;
    }

    // 큐가 꽉 찼는지
    bool full() const {
        portENTER_CRITICAL(&mux_);
        bool f = (count_ >= N);
        portEXIT_CRITICAL(&mux_);
        return f;
    }

private:
    T buf_[N];                  // 고정 크기 링 버퍼 (파이썬 list와 달리 힙 할당 없음)
    volatile int head_;         // 다음에 꺼낼 위치
    volatile int tail_;         // 다음에 넣을 위치
    volatile int count_;        // 현재 아이템 수
    mutable portMUX_TYPE mux_;  // 크로스태스크 보호용 스핀락
};
