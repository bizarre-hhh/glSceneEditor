#ifndef MODEL_NAMES_H
#define MODEL_NAMES_H

#include <QFileInfo>
#include <QSet>
#include <QString>

namespace ModelNames
{
constexpr int kMaxLength = 256;
constexpr int kMaxUtf8Bytes = kMaxLength * 4;

inline QString Normalize(QString name)
{
    name = name.trimmed();
    while (name.endsWith(QStringLiteral(".stl"), Qt::CaseInsensitive))
    {
        name.chop(4);
        name = name.trimmed();
    }
    return name.trimmed();
}

inline bool IsValid(const QString& name)
{
    if (name.isEmpty() || name.size() > kMaxLength || name != Normalize(name))
        return false;
    for (int i = 0; i < name.size(); ++i)
    {
        const QChar character = name[i];
        if (character.isHighSurrogate())
        {
            if (++i == name.size() || !name[i].isLowSurrogate()) return false;
        }
        else if (character.isLowSurrogate() || character.category() == QChar::Other_Control ||
                 character.category() == QChar::Separator_Line ||
                 character.category() == QChar::Separator_Paragraph)
            return false;
    }
    return true;
}

inline QString Truncate(const QString& text, int length)
{
    QString prefix = text.left(length);
    if (!prefix.isEmpty() && prefix.at(prefix.size() - 1).isHighSurrogate()) prefix.chop(1);
    return prefix;
}

inline QString FromPath(const QString& path)
{
    QString name = Truncate(Normalize(QFileInfo(path).fileName()), kMaxLength);
    for (int i = 0; i < name.size(); ++i)
        if (name[i].category() == QChar::Other_Control ||
            name[i].category() == QChar::Separator_Line ||
            name[i].category() == QChar::Separator_Paragraph)
            name[i] = QChar(' ');
    name = Normalize(name);
    return name.isEmpty() ? QStringLiteral("模型") : name;
}

// Reserve each result immediately, including names of copies still being staged.
inline QString Unique(const QString& base, QSet<QString>& occupied)
{
    QString name = base;
    for (int number = 2; occupied.contains(name.toCaseFolded()); ++number)
    {
        const QString suffix = QStringLiteral(" (%1)").arg(number);
        name = Truncate(base, kMaxLength - suffix.size()).trimmed() + suffix;
    }
    occupied.insert(name.toCaseFolded());
    return name;
}
} // namespace ModelNames

#endif