/*
 * Swiftify, a spotify player for desktop
 * Copyright (C) 2026  shred0s
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "playback/Lyrics.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <algorithm>
#include <cmath>
#include <limits>

namespace swiftify {

namespace {

constexpr qint64 MaxTime = qint64(std::numeric_limits<quint32>::max()) - 5000;
constexpr qsizetype SidecarLimit = 512 * 1024;

quint32 boundedTime(qint64 ms)
{
    return quint32(std::clamp(ms, qint64(0), MaxTime));
}

void sortAndFinish(QList<LyricLine> &lines)
{
    std::stable_sort(lines.begin(), lines.end(),
                     [](const LyricLine &a, const LyricLine &b) { return a.startTimeMs < b.startTimeMs; });
    for (qsizetype index = 0; index < lines.size(); ++index) {
        auto &line = lines[index];
        if (line.endTimeMs <= line.startTimeMs) {
            line.endTimeMs = index + 1 < lines.size() ? lines.at(index + 1).startTimeMs : line.startTimeMs + 5000;
        }
        for (qsizetype i = 0; i < line.timings.size(); ++i) {
            auto &word = line.timings[i];
            if (word.endTimeMs <= word.startTimeMs) {
                word.endTimeMs = i + 1 < line.timings.size() ? line.timings.at(i + 1).startTimeMs : line.endTimeMs;
            }
        }
    }
}

quint32 lrcTime(const QRegularExpressionMatch &match)
{
    const QString fraction = match.captured(3);
    const quint32 milliseconds = fraction.isEmpty() ? 0
        : quint32(fraction.toUInt() * (fraction.size() == 1 ? 100 : fraction.size() == 2 ? 10 : 1));
    return quint32(match.captured(1).toUInt() * 60'000 + match.captured(2).toUInt() * 1000) + milliseconds;
}

LyricLine enhancedLine(const QString &source, quint32 start)
{
    static const QRegularExpression mark(QStringLiteral(R"(<(\d{1,4}):([0-5]?\d)(?:[.:](\d{1,3}))?>)"));
    LyricLine result{start, 0, {}, {}};
    qsizetype cursor = 0;
    std::optional<quint32> previous;
    bool ordered = true;
    const auto append = [&](QStringView part, std::optional<quint32> end) {
        const auto trimmed = part.trimmed();
        if (previous && !trimmed.isEmpty()) {
            result.timings.append({*previous, end.value_or(0),
                                   result.words.size() + (trimmed.data() - part.data()), trimmed.size()});
        }
        result.words += part;
    };
    auto markers = mark.globalMatch(source);
    while (markers.hasNext()) {
        const auto match = markers.next();
        const auto time = lrcTime(match);
        ordered = ordered && time >= start && (!previous || time >= *previous);
        append(QStringView(source).sliced(cursor, match.capturedStart() - cursor), time);
        previous = time;
        cursor = match.capturedEnd();
    }
    const auto tail = QStringView(source).sliced(cursor);
    append(tail, std::nullopt);
    if (ordered && previous && tail.trimmed().isEmpty()) {
        result.endTimeMs = *previous;
    }
    if (!ordered) {
        result.timings.clear();
    }
    return result;
}

std::optional<qint64> ttmlTime(QStringView text)
{
    static const QRegularExpression clock(QStringLiteral(R"(^(?:(\d{1,4}):)?([0-5]?\d):([0-5]?\d)(\.\d+)?$)"));
    static const QRegularExpression offset(QStringLiteral(R"(^(\d+(?:\.\d+)?)(ms|s|m|h)$)"));
    const auto stamp = clock.matchView(text);
    double ms = 0;
    if (stamp.hasMatch()) {
        ms = stamp.captured(1).toUInt() * 3'600'000.0 + stamp.captured(2).toUInt() * 60'000.0
             + (stamp.captured(3).toUInt() + stamp.captured(4).toDouble()) * 1000;
    } else {
        const auto duration = offset.matchView(text);
        if (!duration.hasMatch()) {
            return std::nullopt;
        }
        const auto unit = duration.capturedView(2);
        const double scale = unit == u"ms" ? 1.0 : unit == u"s" ? 1000.0 : unit == u"m" ? 60'000.0 : 3'600'000.0;
        ms = duration.captured(1).toDouble() * scale;
    }
    if (!std::isfinite(ms) || ms > MaxTime) {
        return std::nullopt;
    }
    return qint64(std::llround(ms));
}

struct TtmlFrame {
    QString name;
    qint64 start = 0;
    std::optional<qint64> end;
    qsizetype offset = 0;
    bool word = false;
};

QString sidecarPath(const QString &musicPath, QLatin1StringView suffix)
{
    const QFileInfo music(musicPath);
    return music.dir().filePath(music.completeBaseName() + suffix);
}

} // namespace

std::optional<Lyrics> Lyrics::fromLrc(const QString &text)
{
    static const QRegularExpression stamp(QStringLiteral(R"(^\[(\d{1,4}):([0-5]?\d)(?:[.:](\d{1,3}))?\])"));
    static const QRegularExpression tag(QStringLiteral(R"(^\[[a-zA-Z#]+:.*\]\s*$)"));
    static const QRegularExpression offsetTag(QStringLiteral(R"(^\[offset:\s*([+-]?\d+)\s*\]$)"),
                                               QRegularExpression::CaseInsensitiveOption);

    Lyrics lyrics;
    lyrics.provider = QStringLiteral("LRC file");
    qint64 fileOffsetMs = 0;
    QList<LyricLine> plain;
    for (QString line : text.split(u'\n')) {
        if (line.startsWith(QChar::ByteOrderMark)) {
            line.remove(0, 1);
        }
        line = line.trimmed();
        if (const auto offset = offsetTag.match(line); offset.hasMatch()) {
            bool valid = false;
            const int value = offset.captured(1).toInt(&valid);
            if (valid) {
                fileOffsetMs = value;
            }
            continue;
        }
        QList<quint32> starts;
        for (auto match = stamp.match(line); match.hasMatch(); match = stamp.match(line)) {
            starts.append(lrcTime(match));
            line = line.mid(match.capturedLength()).trimmed();
        }
        if (starts.isEmpty()) {
            if (!line.isEmpty() && !tag.match(line).hasMatch()) {
                plain.append({0, 0, line, {}});
            }
            continue;
        }
        const auto parsed = enhancedLine(line, starts.first());
        for (const auto start : starts) {
            auto cue = parsed;
            const qint64 shift = qint64(start) - parsed.startTimeMs;
            cue.startTimeMs = start;
            if (cue.endTimeMs) {
                cue.endTimeMs = boundedTime(qint64(cue.endTimeMs) + shift);
            }
            for (auto &word : cue.timings) {
                word.startTimeMs = boundedTime(qint64(word.startTimeMs) + shift);
                if (word.endTimeMs) {
                    word.endTimeMs = boundedTime(qint64(word.endTimeMs) + shift);
                }
            }
            lyrics.lines.append(std::move(cue));
        }
    }
    lyrics.synchronized = !lyrics.lines.isEmpty();
    if (!lyrics.synchronized) {
        lyrics.lines = std::move(plain);
    } else {
        for (auto &cue : lyrics.lines) {
            cue.startTimeMs = boundedTime(qint64(cue.startTimeMs) - fileOffsetMs);
            if (cue.endTimeMs) {
                cue.endTimeMs = boundedTime(qint64(cue.endTimeMs) - fileOffsetMs);
            }
            for (auto &word : cue.timings) {
                word.startTimeMs = boundedTime(qint64(word.startTimeMs) - fileOffsetMs);
                if (word.endTimeMs) {
                    word.endTimeMs = boundedTime(qint64(word.endTimeMs) - fileOffsetMs);
                }
            }
        }
        sortAndFinish(lyrics.lines);
    }
    return lyrics.lines.isEmpty() ? std::nullopt : std::optional<Lyrics>(std::move(lyrics));
}

std::optional<Lyrics> Lyrics::fromTtml(const QString &text)
{
    QXmlStreamReader xml(text);
    QList<TtmlFrame> stack;
    Lyrics lyrics;
    lyrics.provider = QStringLiteral("TTML file");
    lyrics.synchronized = true;
    LyricLine line;
    bool root = false;
    bool inLine = false;
    bool apple = false;
    bool inWord = false;
    while (!xml.atEnd()) {
        const auto token = xml.readNext();
        if (token == QXmlStreamReader::DTD || token == QXmlStreamReader::EntityReference) {
            return std::nullopt;
        }
        if (xml.isStartElement()) {
            const auto name = xml.name();
            const auto attrs = xml.attributes();
            if (!root) {
                if (name != u"tt" || (!xml.namespaceUri().isEmpty() && xml.namespaceUri() != u"http://www.w3.org/ns/ttml")) {
                    return std::nullopt;
                }
                root = true;
                for (const auto &decl : xml.namespaceDeclarations()) {
                    apple |= decl.namespaceUri().startsWith(u"http://music.apple.com/lyric-ttml-internal");
                }
            }
            const bool timed = name == u"body" || name == u"div" || name == u"p" || name == u"span";
            if (stack.size() >= 64 || (timed && attrs.value(u"timeContainer").compare(u"seq") == 0)) {
                return std::nullopt;
            }
            const auto begin = timed ? attrs.value(u"begin") : QStringView{};
            const auto end = timed ? attrs.value(u"end") : QStringView{};
            const auto dur = timed ? attrs.value(u"dur") : QStringView{};
            const qint64 parentStart = stack.isEmpty() ? 0 : stack.last().start;
            const auto parentEnd = stack.isEmpty() ? std::optional<qint64>{} : stack.last().end;
            const auto beginMs = begin.isEmpty() ? std::optional<qint64>(0) : ttmlTime(begin);
            const auto endMs = end.isEmpty() ? std::optional<qint64>{} : ttmlTime(end);
            const auto durationMs = dur.isEmpty() ? std::optional<qint64>{} : ttmlTime(dur);
            if (!beginMs || (!end.isEmpty() && !endMs) || (!dur.isEmpty() && !durationMs)) {
                return std::nullopt;
            }
            const qint64 base = apple ? 0 : parentStart;
            TtmlFrame frame{name.toString(), begin.isEmpty() ? parentStart : base + *beginMs,
                            parentEnd, 0, false};
            if (endMs) {
                frame.end = frame.end ? std::min(*frame.end, base + *endMs) : base + *endMs;
            }
            if (durationMs) {
                const qint64 candidate = frame.start + *durationMs;
                frame.end = frame.end ? std::min(*frame.end, candidate) : candidate;
            }
            if (name == u"p") {
                if (inLine) {
                    return std::nullopt;
                }
                inLine = true;
                line = {boundedTime(frame.start), frame.end ? boundedTime(*frame.end) : 0, {}, {}};
            } else if (inLine && name == u"span" && !begin.isEmpty() && !inWord) {
                inWord = true;
                frame.word = true;
                frame.offset = line.words.size();
            } else if (inLine && name == u"br") {
                line.words += u' ';
            }
            stack.append(std::move(frame));
        } else if (xml.isCharacters() && inLine) {
            line.words += xml.text();
        } else if (xml.isEndElement() && !stack.isEmpty()) {
            const auto frame = stack.takeLast();
            if (frame.word) {
                inWord = false;
                const auto raw = QStringView(line.words).sliced(frame.offset);
                const auto word = raw.trimmed();
                if (!word.isEmpty() && frame.start >= line.startTimeMs
                    && (!frame.end || *frame.end >= frame.start)
                    && (line.timings.isEmpty() || frame.start >= line.timings.last().startTimeMs)) {
                    line.timings.append({boundedTime(frame.start), frame.end ? boundedTime(*frame.end) : 0,
                                         frame.offset + (word.data() - raw.data()), word.size()});
                }
            } else if (frame.name == u"p") {
                inLine = false;
                if (!line.words.trimmed().isEmpty() && (!frame.end || *frame.end >= frame.start)) {
                    const auto trimmed = QStringView(line.words).trimmed();
                    const qsizetype prefix = trimmed.data() - line.words.constData();
                    for (auto &word : line.timings) {
                        word.offset -= prefix;
                    }
                    line.words = trimmed.toString();
                    lyrics.lines.append(std::move(line));
                }
            }
        }
    }
    if (xml.hasError() || lyrics.lines.isEmpty()) {
        return std::nullopt;
    }
    sortAndFinish(lyrics.lines);
    return lyrics;
}

QString Lyrics::editedPath(const QString &musicPath)
{
    return sidecarPath(musicPath, QLatin1StringView(".swiftify.ttml"));
}

std::optional<Lyrics> Lyrics::besideFile(const QString &musicPath)
{
    for (const char *suffix : {".swiftify.ttml", ".lrc", ".LRC", ".Lrc", ".ttml", ".TTML"}) {
        const QString path = sidecarPath(musicPath, QLatin1StringView(suffix));
        QFile file(path);
        if (!QFileInfo(path).isFile() || !file.open(QIODevice::ReadOnly)) {
            continue;
        }
        const QByteArray data = file.read(SidecarLimit + 1);
        if (data.size() > SidecarLimit) {
            continue;
        }
        const auto text = QString::fromUtf8(data);
        auto parsed = path.endsWith(QLatin1StringView(".ttml"), Qt::CaseInsensitive) ? fromTtml(text) : fromLrc(text);
        if (parsed) {
            return parsed;
        }
    }
    return std::nullopt;
}

QByteArray Lyrics::toTtml() const
{
    const auto time = [](quint32 ms) { return QString::number(ms) + QLatin1StringView("ms"); };
    QByteArray data;
    QXmlStreamWriter xml(&data);
    xml.writeStartDocument();
    xml.writeStartElement(QStringLiteral("tt"));
    xml.writeDefaultNamespace(QStringLiteral("http://www.w3.org/ns/ttml"));
    xml.writeStartElement(QStringLiteral("body"));
    xml.writeStartElement(QStringLiteral("div"));
    for (const auto &line : lines) {
        xml.writeStartElement(QStringLiteral("p"));
        xml.writeAttribute(QStringLiteral("begin"), time(line.startTimeMs));
        xml.writeAttribute(QStringLiteral("end"), time(line.endTimeMs));
        qsizetype position = 0;
        for (const auto &word : line.timings) {
            xml.writeCharacters(line.words.mid(position, word.offset - position));
            xml.writeStartElement(QStringLiteral("span"));
            xml.writeAttribute(QStringLiteral("begin"), time(word.startTimeMs - line.startTimeMs));
            xml.writeAttribute(QStringLiteral("end"), time(word.endTimeMs - line.startTimeMs));
            xml.writeCharacters(line.words.mid(word.offset, word.length));
            xml.writeEndElement();
            position = word.offset + word.length;
        }
        xml.writeCharacters(line.words.mid(position));
        xml.writeEndElement();
    }
    xml.writeEndElement();
    xml.writeEndElement();
    xml.writeEndElement();
    xml.writeEndDocument();
    return data.size() <= SidecarLimit && !xml.hasError() ? data : QByteArray{};
}

bool Lyrics::editLine(int index, quint32 start, const QString &text)
{
    if (!synchronized || index < 0 || index >= lines.size() || text.trimmed().isEmpty()) {
        return false;
    }
    auto &line = lines[index];
    const qint64 delta = qint64(boundedTime(start)) - line.startTimeMs;
    line.startTimeMs = boundedTime(qint64(line.startTimeMs) + delta);
    line.endTimeMs = boundedTime(qint64(line.endTimeMs) + delta);
    if (line.words == text) {
        for (auto &word : line.timings) {
            word.startTimeMs = boundedTime(qint64(word.startTimeMs) + delta);
            word.endTimeMs = boundedTime(qint64(word.endTimeMs) + delta);
        }
    } else {
        line.words = text.trimmed();
        line.timings.clear();
    }
    sortAndFinish(lines);
    return true;
}

std::optional<Lyrics> Lyrics::fromJson(const QByteArray &json)
{
    QJsonParseError error;
    const QJsonObject object = QJsonDocument::fromJson(json, &error).object();
    if (error.error != QJsonParseError::NoError || !object.contains(QStringLiteral("lines"))) {
        return std::nullopt;
    }
    Lyrics lyrics;
    lyrics.language = object.value(QStringLiteral("language")).toString();
    lyrics.provider = object.value(QStringLiteral("provider")).toString();
    lyrics.synchronized = object.value(QStringLiteral("syncType")).toString() == QLatin1StringView("lineSynced");
    for (const QJsonValue &value : object.value(QStringLiteral("lines")).toArray()) {
        const QJsonObject line = value.toObject();
        lyrics.lines.append({
            boundedTime(line.value(QStringLiteral("startTimeMs")).toInteger()),
            boundedTime(line.value(QStringLiteral("endTimeMs")).toInteger()),
            line.value(QStringLiteral("words")).toString(),
            {},
        });
    }
    if (lyrics.synchronized) {
        sortAndFinish(lyrics.lines);
    }
    return lyrics;
}

int Lyrics::lineIndex(qint64 positionMs) const
{
    if (!synchronized || lines.isEmpty()) {
        return -1;
    }
    qsizetype lower = 0;
    qsizetype upper = lines.size();
    while (lower < upper) {
        const qsizetype middle = lower + (upper - lower) / 2;
        if (lines.at(middle).startTimeMs <= positionMs) {
            lower = middle + 1;
        } else {
            upper = middle;
        }
    }
    return int(lower) - 1;
}

int Lyrics::wordIndex(int row, qint64 positionMs) const
{
    if (row < 0 || row >= lines.size()) {
        return -1;
    }
    const auto &words = lines.at(row).timings;
    const auto next = std::upper_bound(words.cbegin(), words.cend(), positionMs,
                                       [](qint64 ms, const LyricWord &word) { return ms < word.startTimeMs; });
    return next != words.cbegin() && positionMs < (next - 1)->endTimeMs ? int(next - words.cbegin()) - 1 : -1;
}

LyricsModel::LyricsModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int LyricsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_lines.size());
}

QVariant LyricsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_lines.size()) {
        return {};
    }
    const LyricLine &line = m_lines.at(index.row());
    switch (role) {
    case WordsRole: return line.words;
    case StartTimeMsRole: return line.startTimeMs;
    case EndTimeMsRole: return line.endTimeMs;
    default: return {};
    }
}

QHash<int, QByteArray> LyricsModel::roleNames() const
{
    return {{WordsRole, "words"}, {StartTimeMsRole, "startTimeMs"}, {EndTimeMsRole, "endTimeMs"}};
}

QString LyricsModel::highlightedWords(int row, int word) const
{
    if (row < 0 || row >= m_lines.size()) {
        return {};
    }
    const auto &line = m_lines.at(row);
    if (word < 0 || word >= line.timings.size()) {
        return line.words.toHtmlEscaped();
    }
    const auto &cue = line.timings.at(word);
    return line.words.left(cue.offset).toHtmlEscaped() + QLatin1StringView("<u>")
        + line.words.mid(cue.offset, cue.length).toHtmlEscaped() + QLatin1StringView("</u>")
        + line.words.mid(cue.offset + cue.length).toHtmlEscaped();
}

void LyricsModel::setLines(const QList<LyricLine> &lines)
{
    beginResetModel();
    m_lines = lines;
    endResetModel();
    emit countChanged();
}

} // namespace swiftify
