/*
 * Copyright © 2016 Andrew Penkrat
 *
 * This file is part of Liri Text.
 *
 * Liri Text is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Liri Text is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Liri Text.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "languageloader.h"
#include <QFile>
#include <QDebug>
#include <QRegularExpression>
#include "languagecontextkeyword.h"
#include "languagecontextsimple.h"
#include "languagecontextcontainer.h"
#include "languagecontextsubpattern.h"
#include "languagemanager.h"

LanguageLoader::LanguageLoader() { }

LanguageLoader::LanguageLoader(QSharedPointer<LanguageDefaultStyles> defaultStyles) {
    for (auto styleId : defaultStyles->styles.keys()) {
        knownStyles[styleId] = QSharedPointer<LanguageStyle>();
    }
}

LanguageLoader::~LanguageLoader() { }

QSharedPointer<LanguageContextReference> LanguageLoader::loadMainContextById(QString id) {
    qDebug() << "Loading" << id;
    QString path = LanguageManager::pathForId(id);
    return loadMainContext(path);
}

QSharedPointer<LanguageContextReference> LanguageLoader::loadMainContextByMimeType(QMimeType mimeType, QString filename) {
    QString path = LanguageManager::pathForMimeType(mimeType, filename);
    return loadMainContext(path);
}

QSharedPointer<LanguageContextReference> LanguageLoader::loadMainContext(QString path) {
    QFile file(path);
    QString langId;
    if(file.open(QFile::ReadOnly)) {
        QXmlStreamReader xml(&file);
        while (!xml.atEnd()) {
            xml.readNext();
            if(xml.isStartElement()) {
                if(xml.name() == "language") {
                    langId = xml.attributes().value("id").toString();
                    languageDefaultOptions   [langId] = QRegularExpression::OptimizeOnFirstUsageOption;
                    languageLeftWordBoundary [langId] = "\\b";
                    languageRightWordBoundary[langId] = "\\b";
                }
                if(xml.name() == "define-regex")
                    parseDefineRegex(xml, langId);
                if(xml.name() == "context")
                    parseContext(xml, langId);
                if(xml.name() == "style")
                    parseStyle(xml, langId);
                if(xml.name() == "default-regex-options")
                    parseDefaultRegexOptions(xml, langId);
                if(xml.name() == "keyword-char-class")
                    parseWordCharClass(xml, langId);
            }
        }
    }
    file.close();
    QString contextId = langId + ":" + langId;
    if(knownContexts.keys().contains(contextId))
        return knownContexts[contextId];
    else
        return QSharedPointer<LanguageContextReference>();
}

LanguageMetadata LanguageLoader::loadMetadata(QString path) {
    LanguageMetadata result;
    QFile file(path);
    if(file.open(QFile::ReadOnly)) {
        QXmlStreamReader xml(&file);
        while (!xml.atEnd()) {
            xml.readNext();
            if(xml.isStartElement()) {
                if(xml.name() == "language") {
                    result.id = xml.attributes().value("id").toString();
                    if(xml.attributes().hasAttribute("_name")) // Translatable
                        result.name = xml.attributes().value("_name").toString();
                    else
                        result.name = xml.attributes().value("name").toString();
                }
                if(xml.name() == "metadata") {
                    parseMetadata(xml, result);
                    break;
                }
            }
        }
    }
    file.close();
    return result;
}

void LanguageLoader::parseMetadata(QXmlStreamReader &xml, LanguageMetadata &metadata) {
    while (!(xml.name() == "metadata" && xml.isEndElement())) {
        xml.readNext();
        if(xml.name() == "property") {
            QStringRef pName = xml.attributes().value("name");
            if(pName == "mimetypes")
                metadata.mimeTypes = xml.readElementText();
            if(pName == "globs")
                metadata.globs = xml.readElementText();
            // Note: metadata can also have line-comment and block-comment properties
        }
    }
}

QSharedPointer<LanguageContextReference> LanguageLoader::parseContext(QXmlStreamReader &xml, QString langId, QXmlStreamAttributes additionalAttributes) {
    QSharedPointer<LanguageContextReference> result;
    QXmlStreamAttributes contextAttributes = xml.attributes();
    contextAttributes += additionalAttributes;
    QString id = contextAttributes.value("id").toString();
    if(!result && id != "" && knownContexts.keys().contains(langId + ":" + id))
        result = knownContexts[langId + ":" + id];
    else
        result = QSharedPointer<LanguageContextReference>(new LanguageContextReference());
    if(contextAttributes.hasAttribute("ref")) {
        QStringRef refId = contextAttributes.value("ref");
        if(refId.contains(':') && !knownContexts.keys().contains(refId.toString())) {
            loadMainContextById(refId.left(refId.indexOf(':')).toString());
        }
        QString refIdCopy = refId.toString();
        if(!refIdCopy.contains(':'))
            refIdCopy = langId + ":" + refIdCopy;
        if(knownContexts.keys().contains(refIdCopy)) {
            result->context = knownContexts[refIdCopy]->context;
            result->style = knownContexts[refIdCopy]->style;
        } else {
            knownContexts[refIdCopy] = result;
        }
    }

    if(id != "")
        knownContexts[langId + ":" + id] = result;

    QString kwPrefix = "\\%[", kwSuffix = "\\%]";

    QString styleId = "";
    if(contextAttributes.hasAttribute("style-ref")) {
        QStringRef styleIdRef = xml.attributes().value("style-ref");
        if(styleIdRef.contains(':') && !knownStyles.keys().contains(styleIdRef.toString()))
            loadMainContextById(styleIdRef.left(styleIdRef.indexOf(':')).toString());
        styleId = styleIdRef.toString();
        if(!styleId.contains(':'))
            styleId = langId + ":" + styleId;
    }
    if(contextAttributes.hasAttribute("ignore-style"))
        result->style.clear();

    if(contextAttributes.hasAttribute("sub-pattern")) {
        if(!result->context)
            result->context = QSharedPointer<LanguageContext>(new LanguageContextSubPattern(contextAttributes));
    }

    xml.readNext();
    while (!(xml.name() == "context" && xml.isEndElement())) {
        if(xml.name() == "start") {
            if(!result->context)
                result->context = QSharedPointer<LanguageContext>(new LanguageContextContainer(contextAttributes));

            auto container = result->context.staticCast<LanguageContextContainer>();
            auto options = parseRegexOptions(xml, langId);
            container->start = resolveRegex((options & QRegularExpression::ExtendedPatternSyntaxOption) != 0 ? xml.readElementText() :
                                                                                            escapeNonExtended( xml.readElementText() ),
                                             options | QRegularExpression::ExtendedPatternSyntaxOption, langId);
        }
        if(xml.name() == "end") {
            if(!result->context)
                result->context = QSharedPointer<LanguageContext>(new LanguageContextContainer(contextAttributes));

            auto container = result->context.staticCast<LanguageContextContainer>();
            auto options = parseRegexOptions(xml, langId);
            container->end = resolveRegex((options & QRegularExpression::ExtendedPatternSyntaxOption) != 0 ? xml.readElementText() :
                                                                                          escapeNonExtended( xml.readElementText() ),
                                           options | QRegularExpression::ExtendedPatternSyntaxOption, langId);
        }
        if(xml.name() == "match") {
            if(!result->context)
                result->context = QSharedPointer<LanguageContext>(new LanguageContextSimple(contextAttributes));

            auto simple = result->context.staticCast<LanguageContextSimple>();
            auto options = parseRegexOptions(xml, langId);
            simple->match = resolveRegex((options & QRegularExpression::ExtendedPatternSyntaxOption) != 0 ? xml.readElementText() :
                                                                                         escapeNonExtended( xml.readElementText() ),
                                          options | QRegularExpression::ExtendedPatternSyntaxOption, langId);
        }
        if(xml.name() == "prefix") {
            /* According to https://developer.gnome.org/gtksourceview/stable/lang-reference.html
             * prefix is a regex in form of define-regex, which means it can have it's own regex options.
             * Howether, in practice none of prebundled languages have them.
             * Futhermore, making prefix an isolated group breaks highlighting for some languages.
             * Following these considerations, prefixes and suffixes are taken in their original form.
             */
            kwPrefix = xml.readElementText();
        }
        if(xml.name() == "suffix") {
            kwSuffix = xml.readElementText();
        }
        if(xml.name() == "keyword") {
            if(!result->context)
                result->context = QSharedPointer<LanguageContext>(new LanguageContextContainer());

            auto kwContainer = result->context.staticCast<LanguageContextContainer>();
            auto inc = QSharedPointer<LanguageContextReference>(new LanguageContextReference);
            inc->context = QSharedPointer<LanguageContext>(new LanguageContextKeyword(contextAttributes));
            auto kw = inc->context.staticCast<LanguageContextKeyword>();
            applyStyleToContext(inc, styleId);

            auto options = parseRegexOptions(xml, langId);
            kw->keyword = resolveRegex(kwPrefix + xml.readElementText() + kwSuffix, options, langId);
            kwContainer->includes.append(inc);
        }
        if(xml.name() == "include") {
            xml.readNext();
            while (!(xml.name() == "include" && xml.isEndElement())) {
                if(xml.name() == "context") {
                    if(!result->context)
                        result->context = QSharedPointer<LanguageContext>(new LanguageContextContainer(contextAttributes));

                    if(result->context->type == LanguageContext::Simple) {
                        auto simple = result->context.staticCast<LanguageContextSimple>();
                        auto inc = parseContext(xml, langId);
                        if(inc)
                            simple->includes.append(inc);
                    } else if(result->context->type == LanguageContext::Container) {
                        auto container = result->context.staticCast<LanguageContextContainer>();

                        QXmlStreamAttributes childrenAttributes;
                        if(container->start.pattern() == "" && contextAttributes.hasAttribute("once-only"))
                            childrenAttributes += QXmlStreamAttribute("once-only", contextAttributes.value("once-only").toString());
                        auto inc = parseContext(xml, langId, childrenAttributes);

                        if(inc)
                            container->includes.append(inc);
                    } else {
                        Q_ASSERT(false);
                    }
                }
                xml.readNext();
            }
        }
        xml.readNext();
    }

    applyStyleToContext(result, styleId);
    return result;
}

QSharedPointer<LanguageStyle> LanguageLoader::parseStyle(QXmlStreamReader &xml, QString langId) {
    auto result = QSharedPointer<LanguageStyle>();
    QString id = xml.attributes().value("id").toString();
    if(xml.attributes().hasAttribute("map-to")) {
        QStringRef refId = xml.attributes().value("map-to");
        if(refId.contains(':') && !knownStyles.keys().contains(refId.toString())) {
            loadMainContextById(refId.left(refId.indexOf(':')).toString());
        }
        QString refIdCopy = refId.toString();
        if(!refIdCopy.contains(':'))
            refIdCopy = langId + ":" + refIdCopy;
        if(knownStyles.keys().contains(refIdCopy)) {
            if(knownStyles[refIdCopy])
                result = knownStyles[refIdCopy];
            else {
                result = QSharedPointer<LanguageStyle>(new LanguageStyle());
                result->defaultId = refIdCopy;
                knownStyles[langId + ":" + id] = result;
            }
        }
    }
    if(result && id != "") {
        knownStyles[langId + ":" + id] = result;
    }
    xml.skipCurrentElement();
    return result;
}

QRegularExpression::PatternOptions LanguageLoader::parseRegexOptions(QXmlStreamReader &xml, QString langId) {
    auto result = languageDefaultOptions[langId];
    if(xml.attributes().hasAttribute("case-sensitive")) {
        bool caseInsensitive = xml.attributes().value("case-sensitive") == "false";
        if(caseInsensitive)
            result |= QRegularExpression::CaseInsensitiveOption;
        else
            result &= ~QRegularExpression::CaseInsensitiveOption;
    }
    if(xml.attributes().hasAttribute("extended")) {
        bool extended = xml.attributes().value("extended") == "true";
        if(extended)
            result |= QRegularExpression::ExtendedPatternSyntaxOption;
        else
            result &= ~QRegularExpression::ExtendedPatternSyntaxOption;
    }
    if(xml.attributes().hasAttribute("dupnames")) {
        // Not supported
    }
    return result;
}

void LanguageLoader::parseDefaultRegexOptions(QXmlStreamReader &xml, QString langId) {
    languageDefaultOptions[langId] = parseRegexOptions(xml, langId);
    xml.readNext();
}

void LanguageLoader::parseDefineRegex(QXmlStreamReader &xml, QString langId) {
    QString id = xml.attributes().value("id").toString();
    auto options = parseRegexOptions(xml, langId);
    knownRegexes[id] = applyOptionsToSubRegex(xml.readElementText(), options);
}

void LanguageLoader::parseWordCharClass(QXmlStreamReader &xml, QString langId) {
    QString charClass = xml.readElementText();
    languageLeftWordBoundary [langId] = QStringLiteral("(?<!%1)(?=%1)").arg(charClass);
    languageRightWordBoundary[langId] = QStringLiteral("(?<=%1)(?!%1)").arg(charClass);
}

QRegularExpression LanguageLoader::resolveRegex(QString pattern, QRegularExpression::PatternOptions options, QString langId) {
    QString resultPattern = pattern;

    for (QString id : knownRegexes.keys()) {
        resultPattern = resultPattern.replace("\\%{" + id + "}", knownRegexes[id]);
    }
    resultPattern = resultPattern.replace("\\%[", languageLeftWordBoundary [langId]);
    resultPattern = resultPattern.replace("\\%]", languageRightWordBoundary[langId]);
    return QRegularExpression(resultPattern, options);
}

QString LanguageLoader::escapeNonExtended(QString pattern) {
    return pattern.replace('#', "\\#").replace(' ', "\\ ");
}

QString LanguageLoader::applyOptionsToSubRegex(QString pattern, QRegularExpression::PatternOptions options) {
    QString result = pattern;
    if((options & QRegularExpression::ExtendedPatternSyntaxOption) == 0)
        result = escapeNonExtended(result);
    if((options & QRegularExpression::CaseInsensitiveOption) != 0)
        result = result.prepend("(?:(?i)").append(")");
    else
        result = result.prepend("(?:(?-i)").append(")");
    return result;
}

void LanguageLoader::applyStyleToContext(QSharedPointer<LanguageContextReference> context, QString styleId) {
    if(knownStyles.keys().contains(styleId)) {
        if(knownStyles[styleId])
            context->style = knownStyles[styleId];
        else {
            context->style = QSharedPointer<LanguageStyle>(new LanguageStyle());
            context->style->defaultId = styleId;
        }
    }
}
