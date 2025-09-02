// fileworker.cpp

#include "fileworker.h"

// For logging move errors.  Qt's debug facilities output messages
// to the appropriate console or log depending on platform.
#include <QDebug>

FileWorker::FileWorker()
    : m_running(true), m_thread(&FileWorker::run, this)
{
}

FileWorker::~FileWorker()
{
    stop();
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void FileWorker::enqueue(const FileTask &task)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push(task);
    }
    m_cv.notify_one();
}

bool FileWorker::cancelTask(const QString &source)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    // Temporary queue to hold tasks we keep
    std::queue<FileTask> temp;
    bool removed = false;
    while (!m_queue.empty()) {
        FileTask t = m_queue.front();
        m_queue.pop();
        if (!removed && t.source == source) {
            // Skip this task
            removed = true;
            continue;
        }
        temp.push(t);
    }
    m_queue = std::move(temp);
    return removed;
}

void FileWorker::stop()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_running = false;
    }
    m_cv.notify_one();
}

void FileWorker::run()
{
    while (true) {
        FileTask task;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this]{ return !m_running || !m_queue.empty(); });
            if (!m_running && m_queue.empty()) {
                break;
            }
            if (!m_queue.empty()) {
                task = m_queue.front();
                m_queue.pop();
            }
        }
        if (!task.source.isEmpty() && !task.destination.isEmpty()) {
            // Perform the file move.  QFile::rename returns true on success.
            // If the rename fails (e.g. due to permissions or files on different
            // volumes), log a warning so the caller can investigate.  Consider
            // adding more robust error handling in the future (copy/delete on
            // failure).
            if (!QFile::rename(task.source, task.destination)) {
                qWarning() << "FileWorker: failed to move" << task.source
                           << "to" << task.destination;
            }
        }
    }
}
