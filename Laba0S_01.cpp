#include <pthread.h>   
#include <unistd.h>    
#include <clocale>     
#include <iostream>

using namespace std;

const unsigned long long NUM_EVENTS = 5;

struct Payload {
    unsigned long long seq;
    const char* info;
    void (*handler)(const Payload*);    
};

static void payload_handler(const Payload* p) {
    cout << "    [обработчик] событие " << p->seq << ": " << p->info << endl;
}
struct Monitor {
    pthread_mutex_t mutex;
    pthread_cond_t  condition;
    bool ready;
    bool stopping;
    void* data;
};
static bool monitor_provide(Monitor* m, void* data) {
    pthread_mutex_lock(&m->mutex);
    while (m->ready && !m->stopping)
        pthread_cond_wait(&m->condition, &m->mutex);

    if (m->stopping) {
        pthread_mutex_unlock(&m->mutex);
        return false;
    }

    m->data = data;
    m->ready = true;
    cout << "[поставщик]  событие "
        << static_cast<Payload*>(data)->seq << " отправлено" << endl;
    pthread_cond_signal(&m->condition);

    pthread_mutex_unlock(&m->mutex);
    return true;
}
static bool monitor_consume(Monitor* m) {
    pthread_mutex_lock(&m->mutex);

    while (!m->ready && !m->stopping)
        pthread_cond_wait(&m->condition, &m->mutex);

    if (!m->ready) {
        pthread_mutex_unlock(&m->mutex);
        return false;
    }

    Payload* p = static_cast<Payload*>(m->data);
    m->data = nullptr;
    m->ready = false;

    cout << "[потребитель] событие " << p->seq << " получено" << endl;
    p->handler(p);                                   
    cout << "[потребитель] событие " << p->seq << " обработано" << endl;
    delete p;

    pthread_cond_signal(&m->condition);
    pthread_mutex_unlock(&m->mutex);
    return true;
}
static void monitor_stop(Monitor* m) {
    pthread_mutex_lock(&m->mutex);
    m->stopping = true;
    pthread_cond_broadcast(&m->condition);
    pthread_mutex_unlock(&m->mutex);
}

static void* producer_run(void* arg) {
    Monitor* m = static_cast<Monitor*>(arg);

    for (unsigned long long seq = 1; NUM_EVENTS == 0 || seq <= NUM_EVENTS; ++seq) {
        sleep(1);
        Payload* p = new Payload{ seq, "вызван несериализуемый обработчик", payload_handler };
        if (!monitor_provide(m, p)) {
            delete p;
            break;
        }
    }

    monitor_stop(m);
    return nullptr;
}

static void* consumer_run(void* arg) {
    Monitor* m = static_cast<Monitor*>(arg);
    while (monitor_consume(m)) {}
    return nullptr;
}

int main() {
    setlocale(LC_ALL, "ru");
    Monitor mon{
        PTHREAD_MUTEX_INITIALIZER,
        PTHREAD_COND_INITIALIZER,
        false, false, nullptr
    };
    cout << "Монитор запущен: " << NUM_EVENTS
        << " событий с интервалом 1 с" << endl << endl;

    pthread_t producer, consumer;
    pthread_create(&producer, nullptr, producer_run, &mon);
    pthread_create(&consumer, nullptr, consumer_run, &mon);
    pthread_join(producer, nullptr);
    pthread_join(consumer, nullptr);
    pthread_cond_destroy(&mon.condition);
    pthread_mutex_destroy(&mon.mutex);
    cout << endl << "Оба потока завершены, монитор уничтожен." << endl;
    return 0;
}