// fileworker.h
//
// Defines a simple background worker that processes file move tasks on a
// separate thread. Utilizes a thread‑safe queue guarded by a mutex and
// condition variable. The worker continually waits for tasks to be
// available and performs file operations without blocking the UI.

#pragma once

#include <QString>
#include <QFile>

#include <queue>
#include <mutex>
#include <thread>
#include <condition_variable>

// A task describing a file move operation: move `source` to `destination`.
struct FileTask
{
    QString source;
    QString destination;
};

class FileWorker
{
public:
    FileWorker();
    ~FileWorker();

    // Enqueue a new move task.  The worker will process it asynchronously.
    void enqueue(const FileTask &task);

    // Attempt to cancel a pending task with the given source path.  If a
    // matching task is found and removed from the queue, return true.
    // If the task is not in the queue (either already processed or not
    // present), return false. This is used by undo logic to remove
    // tasks that have not yet executed.
    bool cancelTask(const QString &source);

    // Stop the worker thread gracefully.  Called during shutdown.
    void stop();

private:
    void run();

    std::queue<FileTask> m_queue;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_running;
    std::thread m_thread;
};
