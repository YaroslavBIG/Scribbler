﻿#include "svgview.h"

SvgView::SvgView(QWidget *parent) : QGraphicsView(parent)
{
    currentScaleFactor = 1.0;
    areBordersHidden = false;
    maxScaleFactor = 1.5;
    minScaleFactor = 0.05;
    itemsToRemove = 0;

    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setDragMode(ScrollHandDrag);
    setRenderHints(QPainter::SmoothPixmapTransform | QPainter::HighQualityAntialiasing);
    limitScale(0.3);

    scene = new QGraphicsScene();
    setScene(scene);

    centerOn(0.0, 0.0);
}

SvgView::~SvgView()
{
    delete scene;
}

void SvgView::wheelEvent(QWheelEvent *event)
{
    qreal factor = qPow(1.2, event->delta() / 240.0);
    limitScale(factor);
    event->accept();
}

void SvgView::limitScale(qreal factor)
{
    qreal newFactor = currentScaleFactor * factor;

    if (newFactor < maxScaleFactor && newFactor > minScaleFactor)
    {
        currentScaleFactor = newFactor;
        scale(factor, factor);
    }
}

int SvgView::renderText(const QStringRef &text)
{
    prepareSceneToRender();
    loadHyphenRules();

    int endOfSheet = 0;

    //Sequentially add the symbols to the scene
    for (int currentSymbolNumber = 0; currentSymbolNumber < text.length(); currentSymbolNumber++)
    {
        QChar symbol = text.at(currentSymbolNumber);

        if (!font.contains(symbol))
        {
            processUnknownSymbol(symbol);
            endOfSheet++;

            if (cursor.x() > currentMarginsRect.bottomRight().x() - (fontSize + letterSpacing) * dpmm)
            {
                cursor.rx() = currentMarginsRect.x();
                cursor.ry() += (fontSize + lineSpacing) * dpmm;
                storedWordItems.push_back(QVector<QGraphicsSvgItem *>());
                storedSymbolData.push_back(QVector<SymbolData>());
            }

            if (cursor.y() > currentMarginsRect.bottomRight().y() - fontSize * dpmm)
                break;

            continue;
        }

        QGraphicsSvgItem *symbolItem = new QGraphicsSvgItem();

        SvgData data = font.values(symbol).at(qrand() % font.values(symbol).size());
        symbolData = data.symbolData;

        symbolItem->setSharedRenderer(data.renderer);
        symbolItem->setScale(data.scale);

        symbolBoundingSize = symbolItem->boundingRect().size() * symbolItem->scale();
        qreal symbolWidth = symbolBoundingSize.width() * symbolData.limits.width();

        preventGoingBeyondRightMargin(symbolWidth, text, currentSymbolNumber);

        //rendering stops by the end of sheet
        if (cursor.y() > currentMarginsRect.bottomRight().y() - fontSize * dpmm)
        {
            delete symbolItem;
            break;
        }

        QPointF symbolItemPos = cursor;
        symbolItemPos.rx() -= symbolBoundingSize.width() * symbolData.limits.left();
        symbolItemPos.ry() -= symbolBoundingSize.height() * symbolData.limits.top();

        symbolItem->setPos(symbolItemPos);
        scene->addItem(symbolItem);

        previousSymbolCursor = cursor;
        previousSymbolData = symbolData;
        previousSymbolWidth = symbolWidth;
        cursor.rx() += symbolWidth + letterSpacing * dpmm;
        endOfSheet++;

        if (symbol.isLetter())
        {
            storedSymbolData.last().push_back(symbolData);
            storedWordItems.last().push_back(symbolItem);
        }
        else
        {
            storedWordItems.push_back(QVector<QGraphicsSvgItem *>());
            storedSymbolData.push_back(QVector<SymbolData>());
        }

    }

    removeLastSymbols();
    endOfSheet -= itemsToRemove;
    connectLetters();
    return endOfSheet;
}

void SvgView::removeLastSymbols()
{
    int itemsCount = scene->items().size();

    if (itemsCount < itemsToRemove)
        return;

    for (int i = 0; i < itemsToRemove; i++)
    {
        while (storedSymbolData.last().isEmpty())
            storedSymbolData.removeLast();
        while (storedWordItems.last().isEmpty())
            storedWordItems.removeLast();

        scene->removeItem(storedWordItems.last().last());
        storedSymbolData.last().removeLast();
        storedWordItems.last().removeLast();
    }
}

void SvgView::prepareSceneToRender()
{
    scene->clear();
    storedSymbolData.clear();
    storedWordItems.clear();
    storedWordItems.push_back(QVector<QGraphicsSvgItem *>());
    storedSymbolData.push_back(QVector<SymbolData>());
    itemsToRemove = 0;

    if (changeMargins)
        currentMarginsRect = QRectF(QPointF(sheetRect.topRight().x() - marginsRect.topRight().x(),
                                            marginsRect.topLeft().y()),
                                    QPointF(sheetRect.bottomRight().x() - marginsRect.bottomLeft().x(),
                                            marginsRect.bottomRight().y()));
    else
        currentMarginsRect = marginsRect;

    scene->addRect(sheetRect);
    scene->addRect(currentMarginsRect, QPen(Qt::darkGray));

    cursor = QPointF(currentMarginsRect.x(), currentMarginsRect.y());

    if (useSeed)
        srand(seed);
    else
        srand(QTime::currentTime().msec());

    hideBorders(areBordersHidden);
}

bool SvgView::preventGoingBeyondRightMargin(qreal symbolWidth, QStringRef text, int currentSymbolIndex)
{
    if (cursor.x() > (currentMarginsRect.x() + currentMarginsRect.width() - symbolWidth))
    {
        bool hyphenateHappened = hyphenate(text, currentSymbolIndex);
        bool wrapWordHappened = false;

        if (!hyphenateHappened)
            wrapWordHappened = wrapWords(text, currentSymbolIndex);

        if (!hyphenateHappened && !wrapWordHappened &&
                !text.at(currentSymbolIndex).isPunct())
        {
            cursor.rx() = currentMarginsRect.x();
            cursor.ry() += (fontSize + lineSpacing) * dpmm;
            storedWordItems.push_back(QVector<QGraphicsSvgItem *>());
            storedSymbolData.push_back(QVector<SymbolData>());
        }

        return true;
    }

    return false;
}

bool SvgView::wrapWords(QStringRef text, int currentSymbolIndex)
{
    int previousSymbolIndex = currentSymbolIndex - 1;

    if (!wordWrap ||
            previousSymbolIndex < 0 || previousSymbolIndex >= text.size() ||
            !(text.at(currentSymbolIndex).isLetterOrNumber() || text.at(currentSymbolIndex).isPunct()) ||
            !(text.at(previousSymbolIndex).isLetterOrNumber() || text.at(previousSymbolIndex).isPunct()))
        return false;

    //wrap all the last letters
    int lastNonLetter = text.toString().lastIndexOf(QRegularExpression("[^\\p{L}]"), currentSymbolIndex);
    int symbolsToWrap = currentSymbolIndex - lastNonLetter - 1;

    if (wrapLastSymbols(symbolsToWrap))
    {
        cursor.rx() = previousSymbolCursor.x() + previousSymbolWidth;
        cursor.ry() += (fontSize + lineSpacing) * dpmm;

        if (cursor.y() > currentMarginsRect.bottomRight().y() - (lineSpacing + fontSize) * dpmm)
            itemsToRemove = symbolsToWrap;

        return true;
    }

    return false;
}

bool SvgView::hyphenate(QStringRef text, int currentSymbolIndex)
{
    if (currentSymbolIndex == 906)
        int a = 0;

    int previousSymbolIndex = currentSymbolIndex - 1;

    if (!hyphenateWords || previousSymbolIndex < 0 ||
            previousSymbolIndex >= text.size() ||
            !text.at(currentSymbolIndex).isLetterOrNumber() ||
            !text.at(previousSymbolIndex).isLetterOrNumber() ||
            cursor.y() - previousSymbolCursor.y() > 0.0000001)
        return false;

    //find word boundary
    int lastNonLetter = text.toString().lastIndexOf(QRegularExpression("[^\\p{L}]"), currentSymbolIndex);
    int nextNonLetter = text.toString().indexOf(QRegularExpression("[^\\p{L}]"), currentSymbolIndex);
    QString word;

    //select a word
    if (nextNonLetter > 0)
        word = text.mid(lastNonLetter + 1, nextNonLetter - lastNonLetter).toString();
    else
        word = text.mid(lastNonLetter + 1, text.size() - 1).toString();

    QString hypher = "\\1-\\2";
    QString hyphenWord = word;

    //divide word into syllables with hyphens
    for (QRegularExpression &rule : hyphenRules)
        hyphenWord.replace(rule, hypher);

    int currentSymbolInWord = currentSymbolIndex - lastNonLetter - 1;

    //find new position of current letter
    for (int i = 0; i <= currentSymbolInWord && currentSymbolInWord < hyphenWord.size(); i++)
        if (hyphenWord.at(i) == '-')
            currentSymbolInWord++;

    //find hyphen, which is nearest to current letter
    qreal indexOfLastHyphen = hyphenWord.lastIndexOf('-', currentSymbolInWord);
    qreal symbolsToWrap = currentSymbolInWord - indexOfLastHyphen - 1;

    //generate hyphens item and hyphenate
    if (indexOfLastHyphen > 0 && symbolsToWrap >= 0)
    {
        QGraphicsSvgItem *hyphen = generateHyphen(symbolsToWrap);

        if (!wrapLastSymbols(symbolsToWrap))
        {
            previousSymbolCursor.rx() = currentMarginsRect.x() - previousSymbolWidth;
            previousSymbolCursor.ry() += (fontSize + lineSpacing) * dpmm;
            storedWordItems.push_back(QVector<QGraphicsSvgItem *>());
            storedSymbolData.push_back(QVector<SymbolData>());
        }

        if (hyphen != nullptr)
            scene->addItem(hyphen);
    }
    else
        return false;

    cursor.rx() = previousSymbolCursor.x() + previousSymbolWidth;
    cursor.ry() = previousSymbolCursor.y();

    if (cursor.y() > currentMarginsRect.bottomRight().y() - (lineSpacing + fontSize) * dpmm)
        itemsToRemove = symbolsToWrap;

    return true;
}

bool SvgView::wrapLastSymbols(int symbolsToWrap)
{
    if (symbolsToWrap <= 0 || symbolsToWrap >= scene->items().size())
        return false;

    //find the first item to wrap and it's position
    int itemsCount = scene->items().size();
    int itemsToWrap = symbolsToWrap; //TODO: consider missing items
    QGraphicsItem * firstItemToWrap = scene->items(Qt::AscendingOrder)[itemsCount - itemsToWrap];
    qreal firstWrapItemPosX = firstItemToWrap->pos().x();

    if (firstWrapItemPosX == currentMarginsRect.x())
        return false;

    //this is how much you need to move symbols to the left
    qreal leftOffset = firstWrapItemPosX - currentMarginsRect.x();

    if (storedSymbolData.last().size() - symbolsToWrap >= 0) //TODO: consider missing items and get rid of this check
    {
        SymbolData firstItemToWrapData = storedSymbolData.last().at(storedSymbolData.last().size() - symbolsToWrap);
        leftOffset += firstItemToWrap->boundingRect().size().width() * firstItemToWrap->scale() * firstItemToWrapData.limits.left();
    }

    previousSymbolCursor.rx() -= leftOffset;
    previousSymbolCursor.ry() += (fontSize + lineSpacing) * dpmm;
    storedWordItems.push_back(QVector<QGraphicsSvgItem *>());
    storedSymbolData.push_back(QVector<SymbolData>());

    //transfer symbols in a new column to half of
    //the wrapped word were not connected with the line
    for (int i = itemsToWrap; i > 0; i--)
    {
        int size = storedWordItems.size();
        int wordSize = storedWordItems.at(size - 2).size();

        if (wordSize <= i)
            break;

        storedWordItems.last().push_back(storedWordItems[size - 2].takeAt(wordSize - i));
        storedSymbolData.last().push_back(storedSymbolData[size - 2].takeAt(wordSize - i));
    }

    //wrap items
    for (int i = itemsToWrap; i > 0; i--)
    {
        QPointF pos = scene->items(Qt::AscendingOrder)[itemsCount - i]->pos();
        pos.rx() -= leftOffset;
        pos.ry() += (fontSize + lineSpacing) * dpmm;
        scene->items(Qt::AscendingOrder)[itemsCount - i]->setPos(pos);
    }

    return true;
}

QGraphicsSvgItem * SvgView::generateHyphen(int symbolsToWrap)
{
    if (!font.contains('-'))
        return nullptr;

    symbolsToWrap++;

    if (symbolsToWrap <= 0)
        return nullptr;

    QGraphicsSvgItem *hyphen = new QGraphicsSvgItem();

    //prepare hyphens item
    SvgData data = font.values('-').at(qrand() % font.values('-').size());
    hyphen->setSharedRenderer(data.renderer);
    hyphen->setScale(data.scale);

    SymbolData hyphenData = data.symbolData;

    //calculate hyphens position
    QSizeF  hyphenBoundingSize = hyphen->boundingRect().size() * hyphen->scale();
    QGraphicsItem* nearestLetter = scene->items(Qt::AscendingOrder).at(scene->items().size() - symbolsToWrap);
    QPointF hyphenPos = cursor;
    hyphenPos.ry() -= hyphenBoundingSize.height() * hyphenData.limits.top();
    hyphenPos.rx() = nearestLetter->pos().x() + nearestLetter->boundingRect().width() * nearestLetter->scale();

    if (storedSymbolData.last().size() - symbolsToWrap >= 0) //TODO: consider missing items and get rid of this check
    {
        SymbolData nearestLetterData = storedSymbolData.last().at(storedSymbolData.last().size() - symbolsToWrap);
        hyphenPos.rx() -=  nearestLetter->boundingRect().width() * nearestLetter->scale() * (1.0 - nearestLetterData.limits.right());
    }

    hyphen->setPos(hyphenPos);

    return hyphen;
}

void SvgView::connectLetters()
{
    if (!connectingLetters)
        return;

    for (int currentWord = 0; currentWord < storedWordItems.size(); currentWord++)
    {
        for (int currentSymbol = 1; currentSymbol < storedWordItems.at(currentWord).size(); currentSymbol++)
        {
            QGraphicsSvgItem * currentLetter = storedWordItems.at(currentWord).at(currentSymbol);
            QGraphicsSvgItem * previousLetter = storedWordItems.at(currentWord).at(currentSymbol - 1);

            //calculate boudingRects; pL means previous letter; cL - current letter
            QSizeF pLBoundingRect, cLBoundingRect;
            pLBoundingRect.setWidth(previousLetter->boundingRect().width() * previousLetter->scale());
            pLBoundingRect.setHeight(previousLetter->boundingRect().height() * previousLetter->scale());
            cLBoundingRect.setWidth(currentLetter->boundingRect().width() * currentLetter->scale());
            cLBoundingRect.setHeight(currentLetter->boundingRect().height() * currentLetter->scale());
            SymbolData pLSymbolData = storedSymbolData.at(currentWord).at(currentSymbol - 1);
            SymbolData cLSymbolData = storedSymbolData.at(currentWord).at(currentSymbol);

            //calculate coordinates of points
            QPointF inPoint, outPoint;
            outPoint.rx() = previousLetter->pos().x() +
                    pLSymbolData.outPoint.x() * pLBoundingRect.width();
            outPoint.ry() = previousLetter->pos().y() +
                    pLSymbolData.outPoint.y() * pLBoundingRect.height();

            inPoint.rx() = currentLetter->pos().x() +
                    cLSymbolData.inPoint.x() * cLBoundingRect.width();
            inPoint.ry() = currentLetter->pos().y() +
                    cLSymbolData.inPoint.y() * cLBoundingRect.height();

            //prepare a pen and draw the line
            QPen pen(fontColor);
            pen.setWidth(penWidth * dpmm);
            pen.setCapStyle(Qt::RoundCap);

            scene->addLine(outPoint.x(), outPoint.y(), inPoint.x(), inPoint.y(), pen);
        }
    }
}

void SvgView::processUnknownSymbol(const QChar &symbol)
{
    switch (symbol.toLatin1())
    {
    case '\t':
        cursor.rx() += wordSpacing * dpmm * spacesInTab - letterSpacing * dpmm;
        break;

    case '\n':
        cursor.rx() = currentMarginsRect.x();
        cursor.ry() += (fontSize + lineSpacing) * dpmm;
        break;

    case ' ':
        cursor.rx() += (wordSpacing - letterSpacing) * dpmm;
        break;

    default:
        cursor.rx() += (fontSize + letterSpacing) * dpmm;
        break;
    }

    storedWordItems.push_back(QVector<QGraphicsSvgItem *>());
    storedSymbolData.push_back(QVector<SymbolData>());
}

QImage SvgView::saveRenderToImage()
{
    QImage image(scene->sceneRect().size().toSize(), QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    scene->render(&painter);

    return image;
}

void SvgView::loadFont(QString fontpath)
{
    if (fontpath.isEmpty())
    {
        QSettings settings("Settings.ini", QSettings::IniFormat);
        settings.beginGroup("Settings");
        fontpath = settings.value("last-used-font", QString()).toString();
        settings.endGroup();
    }

    if (fontpath.isEmpty())
        return;

    QSettings fontSettings(fontpath, QSettings::IniFormat);
    fontSettings.beginGroup("Font");
    fontSettings.setIniCodec(QTextCodec::codecForName("UTF-8"));

    if (fontSettings.allKeys().size() == 0)
    {
        fontSettings.endGroup();
        return;
    }

    //clear the loaded font
    for (SvgData &data : font.values())
    {
        delete data.renderer;
        data.renderer = nullptr;
    }
    font.clear();

    QString fontDirectory = QFileInfo(fontpath).path() + '/';

    //load the data of symbols except the data for capital (uppercase) letters
    for (const QString &key : fontSettings.childKeys())
        for (SymbolData symbolData : fontSettings.value(key).value<QList<SymbolData>>())
        {
            symbolData.fileName = fontDirectory + symbolData.fileName;
            if (key == "slash")
                insertSymbol('/', symbolData);
            else if (key == "backslash")
                insertSymbol('\\', symbolData);
            else
                insertSymbol(key.at(0), symbolData);
        }

    //Load uppercase letters.
    //It's a dirty hack, which helps to distinguish uppercase and lowercase
    //letters on a freaking case-insensetive Windows
    fontSettings.beginGroup("UpperCase");
    for (const QString &key : fontSettings.childKeys())
        for (SymbolData symbolData : fontSettings.value(key).value<QList<SymbolData>>())
        {
            symbolData.fileName = fontDirectory + symbolData.fileName;
            insertSymbol(key.at(0), symbolData);
        }

    fontSettings.endGroup();
    fontSettings.endGroup();

    QSettings settings("Settings.ini", QSettings::IniFormat);
    settings.beginGroup("Settings");
    settings.setValue("last-used-font", QVariant(fontpath));
    settings.endGroup();
}

void SvgView::insertSymbol(QChar key, SymbolData &symbolData)
{
    QSvgRenderer *renderer = new QSvgRenderer(symbolData.fileName);
    qreal symbolHeight = renderer->defaultSize().height() * symbolData.limits.height();
    qreal scale = fontSize * dpmm / symbolHeight;

    //start SVG editing
    QDomDocument doc("SVG");
    QFile file(symbolData.fileName);

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    if (!doc.setContent(&file))
    {
        file.close();
        return;
    }

    file.close();


    QDomElement svgElement = doc.elementsByTagName("svg").item(0).toElement();
    scaleViewBox(svgElement); //scale viewBox to avoid the cut lines with an increase in the width of the line

    //get necessary SVG nodes
    QStringList viewBox = svgElement.attribute("viewBox").split(" ");
    qreal dotsPerUnits = renderer->defaultSize().height() / viewBox.at(3).toDouble();
    qreal newPenWidth = penWidth * dpmm / scale / dotsPerUnits;

    QDomNodeList pathList = doc.elementsByTagName("path");
    QDomNodeList styleList = doc.elementsByTagName("style");

    //change necessary attributes; they can be in "style" nodes or in "path" nodes
    if (!styleList.isEmpty())
    {
        QDomElement element = styleList.item(0).toElement();
        QString style = element.text();
        changeAttribute(style, "stroke-width", QString("%1").arg(newPenWidth));
        if (useCustomFontColor)
            changeAttribute(style, "stroke", fontColor.name(QColor::HexRgb));
        if (roundLines)
        {
            changeAttribute(style, "stroke-linecap", "round");
            changeAttribute(style, "stroke-linejoin", "round");
        }
        QDomElement newElement = doc.createElement("style");
        QDomCDATASection newText = doc.createCDATASection(style);
        newElement.appendChild(newText);
        newElement.setAttribute("type", element.attribute("type", ""));
        element.parentNode().replaceChild(newElement, element);
    }
    else
        for (int i = 0; i < pathList.count(); i++)
        {
            QDomElement element = pathList.at(i).toElement();
            QString style = element.attribute("style", "");
            changeAttribute(style, "stroke-width", QString("%1").arg(newPenWidth));
            if (useCustomFontColor)
                changeAttribute(style, "stroke", fontColor.name(QColor::HexRgb));
            if (roundLines)
            {
                changeAttribute(style, "stroke-linecap", "round");
                changeAttribute(style, "stroke-linejoin", "round");
            }
            element.setAttribute("style", style);
        }

    //load changed symbol
    renderer->load(doc.toString(0).replace(">\n<tspan", "><tspan").toUtf8());
    font.insert(key, {symbolData, scale, renderer});
}

void SvgView::changeAttribute(QString &attribute, QString parameter, QString newValue)
{
    if (attribute.contains(QRegularExpression(parameter + ":")))
    {
        int index = attribute.indexOf(parameter + ":");
        int endSign = attribute.indexOf(QRegularExpression(";|}"), index);
        int valueBegin = index + parameter.size() + 1;

        attribute.remove(valueBegin, endSign - valueBegin);
        attribute.insert(valueBegin, newValue);
    }
    else
    {
        int semicolon = attribute.lastIndexOf(QRegularExpression(";"));
        int endSign = attribute.lastIndexOf(QRegularExpression(";|}"));
        attribute.insert(semicolon > endSign ? semicolon : endSign,
                         (attribute.isEmpty() ? "" : ";") + parameter + ":" + newValue);
    }
}

void SvgView::scaleViewBox(QDomElement &svgElement)
{
    QStringList viewBoxValues = svgElement.attribute("viewBox").split(" ");

    if (viewBoxValues.isEmpty())
        return;

    qreal width = viewBoxValues.at(2).toDouble() - viewBoxValues.at(0).toDouble();
    qreal height = viewBoxValues.at(3).toDouble() - viewBoxValues.at(1).toDouble();

    QString viewBox = QString("%1 %2 %3 %4")
            .arg(static_cast<qreal>(viewBoxValues.at(0).toDouble() - width / 2))
            .arg(static_cast<qreal>(viewBoxValues.at(1).toDouble() - height / 2))
            .arg(static_cast<qreal>(viewBoxValues.at(2).toDouble() + width))
            .arg(static_cast<qreal>(viewBoxValues.at(3).toDouble() + height));

    svgElement.setAttribute("viewBox", viewBox);
}

void SvgView::loadSettingsFromFile()
{
    QSettings settings("Settings.ini", QSettings::IniFormat);
    settings.beginGroup("Settings");
    dpi = settings.value("dpi").toInt();
    dpmm = dpi / 25.4;
    letterSpacing = settings.value("letter-spacing").toDouble();
    lineSpacing =   settings.value("line-spacing").toDouble();
    wordSpacing =   settings.value("word-spacing").toDouble();
    spacesInTab =   settings.value("spaces-in-tab").toInt();
    fontSize =      settings.value("font-size").toDouble();
    penWidth =      settings.value("pen-width").toDouble();
    roundLines =    settings.value("round-lines").toBool();
    useSeed =       settings.value("use-seed").toBool();
    seed =          settings.value("seed").toInt();
    wordWrap =      settings.value("wrap-words").toBool();
    useCustomFontColor = settings.value("use-custom-font-color").toBool();
    connectingLetters =     settings.value("connect-letters").toBool();
    hyphenateWords =     settings.value("hyphenate-words").toBool();

    sheetRect = QRectF(0, 0,
                       settings.value("sheet-width").toInt() * dpmm,
                       settings.value("sheet-height").toInt() * dpmm);

    marginsRect = QRectF(sheetRect.topLeft() + QPointF(settings.value("left-margin").toInt() * dpmm,
                                                       settings.value("top-margin").toInt() * dpmm),
                         sheetRect.bottomRight() - QPointF(settings.value("right-margin").toInt() * dpmm,
                                                           settings.value("bottom-margin").toInt() * dpmm));

    fontColor = QColor(settings.value("font-color").toString());

    loadFont(settings.value("last-used-font", "Font/DefaultFont.ini").toString());
    settings.endGroup();

    scene->setSceneRect(sheetRect);
    renderText();
}

void SvgView::loadHyphenRules()
{
    hyphenRules.clear();
    //load variables with their values first
    QMap<QString, QString> variables;
    QSettings settings("hyphenationRules.ini", QSettings::IniFormat);
    settings.beginGroup("Variables");

    for (const QString &name : settings.childKeys())
        variables.insert(name, QString::fromUtf8(settings.value(name).toString().toLatin1()));

    //than load rules
    settings.endGroup();
    settings.beginGroup("Rules");

    for (const QString &key : settings.childKeys())
    {
        QString rule = settings.value(key).toString();

        //and replace variables on their values
        for (QString &variable : variables.uniqueKeys())
            rule.replace(variable, variables[variable]);

        hyphenRules.push_back(QRegularExpression(rule));
    }

    settings.endGroup();
}

void SvgView::hideBorders(bool hide)
{
    areBordersHidden = hide;
    scene->items(Qt::AscendingOrder).at(0)->setVisible(!hide); //sheetRect
    scene->items(Qt::AscendingOrder).at(1)->setVisible(!hide); //marginsRect
}

void SvgView::changeLeftRightMargins(bool change)
{
    changeMargins = change;
}
