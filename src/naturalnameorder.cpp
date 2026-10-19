#include "naturalnameorder.h"

#include <QFileInfo>

#include <algorithm>

namespace
{

int endOfDigits(QStringView text, int start)
{
    int end = start;
    while (end < text.size() && text.at(end).isDigit())
        ++end;
    return end;
}

int compareDigitRun(QStringView left, QStringView right)
{
    int i = 0;
    int j = 0;
    while (i + 1 < left.size() && left.at(i) == QLatin1Char('0'))
        ++i;
    while (j + 1 < right.size() && right.at(j) == QLatin1Char('0'))
        ++j;
    const int lenLeft = left.size() - i;
    const int lenRight = right.size() - j;
    if (lenLeft != lenRight)
        return lenLeft < lenRight ? -1 : 1;
    const int cmp = left.mid(i).compare(right.mid(j));
    if (cmp != 0)
        return cmp < 0 ? -1 : 1;
    if (left.size() != right.size())
        return left.size() < right.size() ? -1 : 1;
    return 0;
}

} // namespace

int compareNaturalName(QStringView left, QStringView right)
{
    int i = 0;
    int j = 0;
    while (i < left.size() && j < right.size())
    {
        const bool leftDigit = left.at(i).isDigit();
        const bool rightDigit = right.at(j).isDigit();
        if (leftDigit || rightDigit)
        {
            if (leftDigit != rightDigit)
                return leftDigit ? -1 : 1;
            const int endLeft = endOfDigits(left, i);
            const int endRight = endOfDigits(right, j);
            const int cmp = compareDigitRun(left.mid(i, endLeft - i), right.mid(j, endRight - j));
            if (cmp != 0)
                return cmp;
            i = endLeft;
            j = endRight;
            continue;
        }
        int endLeft = i;
        while (endLeft < left.size() && !left.at(endLeft).isDigit())
            ++endLeft;
        int endRight = j;
        while (endRight < right.size() && !right.at(endRight).isDigit())
            ++endRight;
        const int cmp =
            left.mid(i, endLeft - i).compare(right.mid(j, endRight - j), Qt::CaseInsensitive);
        if (cmp != 0)
            return cmp < 0 ? -1 : 1;
        i = endLeft;
        j = endRight;
    }
    if (i == left.size() && j == right.size())
        return 0;
    return i == left.size() ? -1 : 1;
}

QStringList sortPathsByNaturalFileName(QStringList paths)
{
    std::stable_sort(paths.begin(), paths.end(),
                     [](const QString &a, const QString &b)
                     {
                         const int byName =
                             compareNaturalName(QFileInfo(a).fileName(), QFileInfo(b).fileName());
                         if (byName != 0)
                             return byName < 0;
                         return QString::compare(a, b, Qt::CaseSensitive) < 0;
                     });
    return paths;
}
