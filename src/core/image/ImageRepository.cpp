#include "core/image/ImageRepository.h"

#include "core/cache/CacheManager.h"
#include "core/filesystem/FileSystem.h"
#include "core/image/Decoder.h"
#include "core/image/DiskCache.h"
#include "core/image/ImageFrame.h"
#include "core/image/MetadataReader.h"
#include "core/scheduler/TaskScheduler.h"
#include "core/trace/Trace.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QImageReader>
#include <atomic>
#include <condition_variable>
#include <exception>
#include <filesystem>
#include <future>
#include <map>
#include <mutex>
#include <thread>

ImageRepository &ImageRepository::instance()
{
    static ImageRepository inst;
    return inst;
}

void ImageRepository::setSyncLoadBudget(std::chrono::milliseconds budget)
{
    m_syncLoadBudgetMs.store(budget.count());
}

std::chrono::milliseconds ImageRepository::syncLoadBudget() const
{
    return std::chrono::milliseconds(m_syncLoadBudgetMs.load());
}

std::string ImageRepository::makeKey(const std::string &filePath) const
{
    const std::string key = mviewer::core::MetadataReader::key(filePath);
    rememberKey(filePath, key);
    return key;
}

std::string ImageRepository::cachedKeyForPath(const std::string &filePath) const
{
    std::lock_guard<std::mutex> lock(m_keyMtx);
    const auto it = m_keyByPath.find(filePath);
    return it == m_keyByPath.end() ? std::string() : it->second;
}

void ImageRepository::rememberKey(const std::string &filePath, const std::string &key) const
{
    std::lock_guard<std::mutex> lock(m_keyMtx);
    m_keyByPath[filePath] = key;
}

void ImageRepository::forgetKey(const std::string &filePath) const
{
    std::lock_guard<std::mutex> lock(m_keyMtx);
    m_keyByPath.erase(filePath);
}

void ImageRepository::invalidateAll()
{
    DiskCache::instance().clear();
    CacheManager::instance().clearMemory();
    std::lock_guard<std::mutex> lock(m_keyMtx);
    m_keyByPath.clear();
}

mviewer::domain::ImageMetadata ImageRepository::makeMeta(const std::string &filePath) const
{
    return mviewer::core::MetadataReader::read(filePath);
}

bool ImageRepository::getPreviewCache(const std::string &key, ImageData &out) const
{
    return CacheManager::instance().getMemory(CacheLevel::Preview, key, out);
}

void ImageRepository::putPreviewCache(const std::string &key, const ImageData &image)
{
    CacheManager::instance().putMemory(CacheLevel::Preview, key, image);
}

