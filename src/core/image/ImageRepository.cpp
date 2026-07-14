#include "core/image/ImageRepository.h"

#include "core/cache/CacheManager.h"
#include "core/filesystem/FileSystem.h"
#include "core/image/Decoder.h"
#include "core/image/DiskCache.h"
#include "core/scheduler/TaskScheduler.h"

#include <QFileInfo>
#include <QImageReader>
#include <QSize>
#include <QTimer>

#include <atomic>
#include <future>

const ImageRepository::LoadOptions ImageRepository::kDefaultLoadOptions{};

ImageRepository &ImageRepository::instance() {
  static ImageRepository inst;
  return inst;
}

std::string ImageRepository::makeKey(const std::string &filePath) const {
  const QFileInfo fi(QString::fromStdString(filePath));
  const QString key = QString::fromStdString(filePath) +
                      QString::number(fi.size()) +
                      QString::number(fi.lastModified().toSecsSinceEpoch());
  return key.toStdString();
}

mviewer::domain::ImageMetadata
ImageRepository::makeMeta(const std::string &filePath) const {
  mviewer::domain::ImageMetadata meta;
  const QFileInfo fi(QString::fromStdString(filePath));
  if (!fi.exists())
    return meta;
  meta.filePath = filePath;
  meta.fileName = fi.fileName().toStdString();
  meta.fileSize = fi.size();
  meta.modifiedEpochSec = fi.lastModified().toSecsSinceEpoch();
  QImageReader reader(QString::fromStdString(filePath));
  const QSize s = reader.size();
  meta.width = s.width();
  meta.height = s.height();
  meta.hash = filePath + "|" + std::to_string(meta.fileSize) + "|" +
              std::to_string(meta.modifiedEpochSec);
  return meta;
}

ImageRepository::Result ImageRepository::load(const std::string &filePath,
                                              const LoadOptions &opts) {
  Result res;
  const std::string key = makeKey(filePath);

  ImageData img;
  bool fromCache = false;
  if (opts.useDiskCache && DiskCache::instance().get(key, img)) {
    fromCache = true;
  } else {
    img = Decoder::decodeFull(filePath);
    if (img.isNull()) {
      res.error = "decode failed: " + filePath;
      return res;
    }
    if (opts.useDiskCache)
      DiskCache::instance().put(key, img);
  }

  auto frame = std::make_shared<ImageFrame>(ImageFrame::create(filePath, img));
  if (opts.generateHistogram)
    frame->computeHistogram();
  frame->setDecodeState(DecodeState::Decoded);
  frame->setCacheState(fromCache ? CacheState::Disk : CacheState::None);

  CacheManager::instance().putMetadata(key, frame->metadata());

  res.frame = frame;
  res.fromCache = fromCache;
  return res;
}

void ImageRepository::loadAsync(const std::string &filePath,
                                std::function<void(const Result &)> callback,
                                const LoadOptions &opts) {
  auto out = std::make_shared<Result>();
  TaskScheduler::instance().submit(
      TaskScheduler::DecodePool,
      [this, filePath, opts, out]() { *out = load(filePath, opts); },
      [callback, out]() {
        if (callback)
          callback(*out);
      });
}

std::vector<ImageRepository::Result>
ImageRepository::loadDirectory(const std::string &dirPath, int maxImages) {
  const std::vector<std::string> files =
      FileSystem::listImages(dirPath, maxImages);
  if (files.empty())
    return {};

  const int n = static_cast<int>(files.size());
  std::vector<Result> results(n);
  std::atomic<int> completed{0};

  for (int i = 0; i < n; ++i) {
    TaskScheduler::instance().submit(TaskScheduler::Priority::Decode,
                                     [this, &files, &results, &completed, n,
                                      i](const TaskScheduler::TaskContext &) {
                                       results[i] = load(files[i]);
                                       completed.fetch_add(
                                           1, std::memory_order_release);
                                     });
  }

  while (completed.load(std::memory_order_acquire) < n) {
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }
  return results;
}

void ImageRepository::loadDirectoryAsync(
    const std::string &dirPath,
    std::function<void(const std::vector<Result> &)> callback, int maxImages) {
  const std::vector<std::string> files =
      FileSystem::listImages(dirPath, maxImages);
  if (files.empty()) {
    if (callback)
      callback({});
    return;
  }

  const int n = static_cast<int>(files.size());
  auto results = std::make_shared<std::vector<Result>>(n);
  auto completed = std::make_shared<std::atomic<int>>(0);
  auto callbackPtr =
      std::make_shared<std::function<void(const std::vector<Result> &)>>(
          std::move(callback));

  for (int i = 0; i < n; ++i) {
    TaskScheduler::instance().submit(
        TaskScheduler::Priority::Decode,
        [this, &files, results, completed, n, i,
         callbackPtr](const TaskScheduler::TaskContext &) {
          (*results)[i] = load(files[i]);
          int prev = completed->fetch_add(1, std::memory_order_acq_rel);
          if (prev + 1 == n) {
            auto *cb = callbackPtr.get();
            if (*cb) {
              auto func = *cb;
              QTimer::singleShot(0, [func, results]() { func(*results); });
            }
          }
        });
  }
}

void ImageRepository::prefetchVisible(
    const std::vector<std::string> &visiblePaths,
    const std::vector<std::string> &adjacentPaths) {
  for (const auto &p : visiblePaths) {
    TaskScheduler::instance().submit(
        TaskScheduler::Priority::UI,
        [this, p](const TaskScheduler::TaskContext &) { load(p); });
  }

  for (const auto &p : adjacentPaths) {
    TaskScheduler::instance().submit(
        TaskScheduler::Priority::Background,
        [this, p](const TaskScheduler::TaskContext &) { load(p); });
  }
}

void ImageRepository::prefetch(const std::vector<std::string> &keys,
                               CacheLevel level) {
  if (level == CacheLevel::Disk)
    return;
  for (const auto &key : keys) {
    ImageData img;
    if (CacheManager::instance().getDisk(key, img))
      CacheManager::instance().putMemory(level, key, img);
  }
}

void ImageRepository::release(const std::string &filePath) {
  CacheManager::instance().invalidate(makeKey(filePath));
}

mviewer::domain::ImageMetadata
ImageRepository::metadata(const std::string &filePath) const {
  const std::string key = makeKey(filePath);
  mviewer::domain::ImageMetadata meta;
  if (CacheManager::instance().getMetadata(key, meta))
    return meta;
  meta = makeMeta(filePath);
  if (!meta.filePath.empty())
    CacheManager::instance().putMetadata(key, meta);
  return meta;
}

void ImageRepository::cacheToDisk(const std::string &filePath) {
  const std::string key = makeKey(filePath);
  ImageData img = Decoder::decodeFull(filePath);
  if (!img.isNull())
    DiskCache::instance().put(key, img);
}

void ImageRepository::invalidate(const std::string &filePath) {
  CacheManager::instance().invalidate(makeKey(filePath));
}

void ImageRepository::invalidateAll() {
  DiskCache::instance().clear();
  CacheManager::instance().clearMemory();
}
