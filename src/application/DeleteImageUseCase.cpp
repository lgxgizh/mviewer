#include "DeleteImageUseCase.h"
#include <QFile>

DeleteImageUseCase::Result
DeleteImageUseCase::execute(const std::string &filePath) {
  Result r;
  QString qPath = QString::fromStdString(filePath);
  if (!QFile::moveToTrash(qPath)) {
    r.error = "Failed to move file to recycle bin";
    return r;
  }
  r.success = true;
  return r;
}
