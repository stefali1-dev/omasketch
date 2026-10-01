#include "pathcompleter.h"

#include <QDir>
#include <QFileInfo>

PathCompleter::PathCompleter(QObject *parent)
    : QObject(parent)
{
}

QVariantMap PathCompleter::complete(const QString &text, bool openMode)
{
    m_matches.clear();
    m_index = -1;
    m_base.clear();

    // Split into the folder to list and the segment to match, keeping "~"
    // in the display form.
    QString base, segment;
    const int cut = text.lastIndexOf(u'/');
    if (text.startsWith(u'~')) {
        base = cut < 0 ? QDir::homePath() + '/' : expand(text.left(cut + 1));
        segment = cut < 0 ? text.mid(1) : text.mid(cut + 1);
    } else {
        base = cut < 0 ? QString() : expand(text.left(cut + 1));
        segment = cut < 0 ? text : text.mid(cut + 1);
    }

    const QDir dir(base);
    if (!dir.exists())
        return {{"text", text}, {"ghost", QString()}, {"count", 0}};

    // Hidden files appear only when asked for, like in a shell.
    QDir::Filters filters = QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot;
    if (segment.startsWith(u'.'))
        filters |= QDir::Hidden;

    QStringList matches;
    const QFileInfoList entries =
        dir.entryInfoList(filters, QDir::Name | QDir::DirsFirst | QDir::IgnoreCase);
    for (const QFileInfo &entry : entries) {
        if (!entry.fileName().startsWith(segment))
            continue;
        if (openMode && !entry.isDir()
            && !entry.fileName().endsWith(QStringLiteral(".png"), Qt::CaseInsensitive))
            continue;
        matches << (entry.isDir() ? entry.fileName() + '/' : entry.fileName());
    }
    if (matches.isEmpty())
        return {{"text", text}, {"ghost", QString()}, {"count", 0}};

    m_base = base;
    m_matches = matches;

    QString common = matches.first();
    for (const QString &match : matches) {
        while (!match.startsWith(common))
            common.chop(1);
    }
    return {{"text", shorten(m_base + common)},
            {"ghost", matches.first().mid(common.size())},
            {"count", matches.size()}};
}

QVariantMap PathCompleter::cycle(int step)
{
    const int n = m_matches.size();
    if (n < 1)
        return {{"text", QString()}, {"ghost", QString()}, {"count", 0}};
    if (m_index < 0)
        m_index = step > 0 ? 0 : n - 1;
    else
        m_index = (m_index + step + n) % n;
    return {{"text", shorten(m_base + m_matches.at(m_index))},
            {"ghost", QString()},
            {"count", n}};
}

void PathCompleter::reset()
{
    m_matches.clear();
    m_index = -1;
}

QString PathCompleter::expand(const QString &path) const
{
    if (path == QLatin1String("~"))
        return QDir::homePath();
    if (path.startsWith(u'~'))
        return QDir::homePath() + (path.startsWith(QStringLiteral("~/"))
                                       ? path.mid(1) : '/' + path.mid(1));
    return path;
}

QString PathCompleter::shorten(const QString &path) const
{
    const QString home = QDir::homePath();
    if (path == home)
        return QStringLiteral("~");
    if (path.startsWith(home + '/'))
        return QLatin1Char('~') + path.mid(home.size());
    return path;
}

bool PathCompleter::exists(const QString &path) const
{
    return QFileInfo::exists(expand(path));
}

bool PathCompleter::isDir(const QString &path) const
{
    return QFileInfo(expand(path)).isDir();
}

QVariantMap PathCompleter::killPrevWord(const QString &text, int pos) const
{
    const auto delimiter = [](QChar c) { return c == u'/' || c.isSpace(); };
    int start = pos;
    while (start > 0 && delimiter(text.at(start - 1)))
        --start;
    while (start > 0 && !delimiter(text.at(start - 1)))
        --start;
    return {{"text", text.left(start) + text.mid(pos)}, {"cursor", start}};
}
