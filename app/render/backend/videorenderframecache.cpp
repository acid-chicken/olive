#include "videorenderframecache.h"

#include <QDir>
#include <QFileInfo>

#include "common/filefunctions.h"

VideoRenderFrameCache::VideoRenderFrameCache() {}

void VideoRenderFrameCache::Clear() {
  time_hash_map_.clear();

  {
    QMutexLocker locker(&currently_caching_lock_);
    currently_caching_list_.clear();
  }

  cache_id_.clear();
}

bool VideoRenderFrameCache::HasHash(const QByteArray &hash, const PixelFormat::Format &format) {
  return QFileInfo::exists(CachePathName(hash, format)) && !IsCaching(hash);
}

bool VideoRenderFrameCache::IsCaching(const QByteArray &hash) {
  QMutexLocker locker(&currently_caching_lock_);

  return currently_caching_list_.contains(hash);
}

bool VideoRenderFrameCache::TryCache(const QByteArray &hash) {
  QMutexLocker locker(&currently_caching_lock_);

  if (!currently_caching_list_.contains(hash)) {
    currently_caching_list_.append(hash);
    return true;
  }

  return false;
}

void VideoRenderFrameCache::SetCacheID(const QString &id) {
  Clear();

  cache_id_ = id;
}

QByteArray VideoRenderFrameCache::TimeToHash(const rational &time) const { return time_hash_map_.value(time); }

void VideoRenderFrameCache::SetHash(const rational &time, const QByteArray &hash) { time_hash_map_.insert(time, hash); }

void VideoRenderFrameCache::Truncate(const rational &time) {
  QMap<rational, QByteArray>::iterator i = time_hash_map_.begin();

  while (i != time_hash_map_.end()) {
    if (i.key() >= time) {
      i = time_hash_map_.erase(i);
    } else {
      i++;
    }
  }
}

void VideoRenderFrameCache::RemoveHashFromCurrentlyCaching(const QByteArray &hash) {
  QMutexLocker locker(&currently_caching_lock_);

  currently_caching_list_.removeOne(hash);
}

QList<rational> VideoRenderFrameCache::FramesWithHash(const QByteArray &hash) const {
  QList<rational> times;

  QMap<rational, QByteArray>::const_iterator iterator;

  for (iterator = time_hash_map_.begin(); iterator != time_hash_map_.end(); iterator++) {
    if (iterator.value() == hash) {
      times.append(iterator.key());
    }
  }

  return times;
}

QList<rational> VideoRenderFrameCache::TakeFramesWithHash(const QByteArray &hash) {
  QList<rational> times;

  QMap<rational, QByteArray>::iterator iterator = time_hash_map_.begin();

  while (iterator != time_hash_map_.end()) {
    if (iterator.value() == hash) {
      times.append(iterator.key());

      iterator = time_hash_map_.erase(iterator);
    } else {
      iterator++;
    }
  }

  return times;
}

const QMap<rational, QByteArray> &VideoRenderFrameCache::time_hash_map() const { return time_hash_map_; }

QString VideoRenderFrameCache::CachePathName(const QByteArray &hash, const PixelFormat::Format &pix_fmt) const {
  QString ext;

  if (pix_fmt == PixelFormat::PIX_FMT_RGBA8 || pix_fmt == PixelFormat::PIX_FMT_RGBA16U) {
    // For some reason, integer EXRs are extremely slow to load, so we use TIFF instead.
    ext = QStringLiteral("tiff");
  } else {
    ext = QStringLiteral("exr");
  }

  QDir cache_dir(QDir(GetMediaCacheLocation()).filePath(QString(hash.left(1).toHex())));
  cache_dir.mkpath(".");

  QString filename = QStringLiteral("%1.%2").arg(QString(hash.mid(1).toHex()), ext);

  return cache_dir.filePath(filename);
}
