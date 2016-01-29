#include "fontdialog.h"
#include "ui_fontdialog.h"

FontDialog::FontDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::FontDialog)
{
    ui->setupUi(this);

    svgWidget = new QSvgWidget();

    QPalette svgPalette = svgWidget->palette();
    svgPalette.setColor(QPalette::Background, Qt::white);
    svgWidget->setAutoFillBackground(true);
    svgWidget->setPalette(svgPalette);

    ui->horizontalLayout_4->addWidget(svgWidget);

    connect(ui->choosenSymbolTextEdit, SIGNAL(textChanged()),
            this, SLOT(limitTextEdit()));
    connect(ui->fontFilePushButton, SIGNAL(clicked()),
            this, SLOT(loadFont()));
    connect(ui->SymbolFilesPushButton, SIGNAL(clicked()),
            this, SLOT(loadLetters()));
    connect(ui->buttonBox, SIGNAL(accepted()),
            this, SLOT(saveFont()));
    connect(ui->buttonBox, SIGNAL(rejected()),
            this, SLOT(rejectChanges()));
    connect(ui->treeWidget, SIGNAL(itemPressed(QTreeWidgetItem*, int)),
            this, SLOT(setTextFromItem(QTreeWidgetItem*)));
    connect(ui->deleteSymbolButton, SIGNAL(pressed()),
            this, SLOT(deleteLetter()));

    ui->SymbolFilesPushButton->setEnabled(false);
    ui->fontFileTextEdit->setLineWrapMode(QTextEdit::NoWrap);
    ui->fontFileTextEdit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ui->fontFileTextEdit->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    ui->treeWidget->setColumnCount(1);
}

FontDialog::~FontDialog()
{
    delete ui;
    delete svgWidget;
}

void FontDialog::loadFont()
{
    fontFileName.clear();
    fontFileName = QFileDialog::getSaveFileName(0, tr("Choose"), "",
                                                   tr("INI") +
                                                      "(*.ini);;" +
                                                   tr("All Files") +
                                                      "(*.*)");
    if (fontFileName.isEmpty())
        return;

    ui->fontFileTextEdit->setText(fontFileName);
    ui->SymbolFilesPushButton->setEnabled(true);

    QSettings fontSettings(fontFileName, QSettings::IniFormat);
    fontSettings.beginGroup("Font");
    fontSettings.setIniCodec(QTextCodec::codecForName("UTF-8"));

    if (fontSettings.allKeys().size() == 0)
    {
        fontSettings.endGroup();
        return;
    }

    font.clear();
    for (const QString &key : fontSettings.childKeys())
        for (const Letter &value : fontSettings.value(key).value<QList<Letter>>())
            font.insert(key.at(0).toLower(), value);

    //It's a dirty hack, which helps to distinguish uppercase and lowercase
    //letters on freaking case-insensetive Windows
    fontSettings.beginGroup("UpperCase");
    for (const QString &key : fontSettings.childKeys())
        for (const Letter &value : fontSettings.value(key).value<QList<Letter>>())
            font.insert(key.at(0).toUpper(), value);
    fontSettings.endGroup();

    fontSettings.endGroup();

    ui->treeWidget->clear();

    for (QChar key : font.uniqueKeys())
    {
        QTreeWidgetItem *letterItem = new QTreeWidgetItem(static_cast<QTreeWidget *>(nullptr), QStringList(QString(key)));
        for (const Letter & value : font.values(key))
        {
            QTreeWidgetItem *valueItem = new QTreeWidgetItem(letterItem, QStringList(value.fileName));
            letterItem->addChild(valueItem);
        }
        ui->treeWidget->insertTopLevelItem(ui->treeWidget->topLevelItemCount(), letterItem);
    }
}

void FontDialog::loadLetters()
{
    QStringList files = QFileDialog::getOpenFileNames(0, tr("Choose"), "",
                                                         tr("SVG") +
                                                            "(*.svg);;" +
                                                         tr("All Files") +
                                                            "(*.*)");
    if (files.isEmpty())
        return;

    QChar letter = ui->choosenSymbolTextEdit->toPlainText().at(0);

    QTreeWidgetItem * letterItem;

    if (font.contains(letter))
    {
        for (const Letter & value : font.values(letter))
            files += value.fileName;

        files.removeDuplicates();
        font.remove(letter);
        letterItem = ui->treeWidget->findItems(letter, Qt::MatchCaseSensitive).first();
        delete ui->treeWidget->takeTopLevelItem(ui->treeWidget->indexOfTopLevelItem(letterItem));
    }

    letterItem = new QTreeWidgetItem(static_cast<QTreeWidget *>(nullptr), QStringList(QString(letter)));

    letterItem->text(0);

    for (QString fileName : files)
        font.insert(letter, {QFileInfo(fileName).fileName()});

    for (QString value : files)
    {
        QTreeWidgetItem *valueItem = new QTreeWidgetItem(letterItem, QStringList(QFileInfo(value).fileName()));
        letterItem->addChild(valueItem);


    }
    ui->treeWidget->insertTopLevelItem(ui->treeWidget->topLevelItemCount(), letterItem);

    ui->choosenSymbolTextEdit->setText(QString());
}

void FontDialog::saveFont()
{
    QFile file (fontFileName);
    file.remove();

    QSettings fontSettings(fontFileName, QSettings::IniFormat);
    fontSettings.beginGroup("Font");
    fontSettings.setIniCodec(QTextCodec::codecForName("UTF-8"));

    for (QChar &key : font.keys())
        if (key.isUpper())
        {
            fontSettings.beginGroup("UpperCase");
            fontSettings.remove(key);
            fontSettings.setValue(key, QVariant::fromValue(font.values(key)));
            fontSettings.endGroup();
        }
        else
        {
            fontSettings.remove(key);
            fontSettings.setValue(key, QVariant::fromValue(font.values(key)));
        }

    fontSettings.endGroup();

    emit fontReady();
}

void FontDialog::rejectChanges()
{
    font.clear();
    fontFileName.clear();
    ui->SymbolFilesPushButton->setEnabled(false);
    ui->choosenSymbolTextEdit->clear();
    ui->fontFileTextEdit->clear();
    ui->treeWidget->clear();
}

void FontDialog::limitTextEdit()
{
    QString text = ui->choosenSymbolTextEdit->toPlainText();
    if (text.length() > 1)
    {
        text = text.left(1);
        ui->choosenSymbolTextEdit->setText(text);
    }
}

void FontDialog::setTextFromItem(QTreeWidgetItem * item)
{
    if (item->parent() == nullptr)
        ui->choosenSymbolTextEdit->setText(item->text(0));
    else
    {
        ui->choosenSymbolTextEdit->setText(item->parent()->text(0));
        svgWidget->load(QFileInfo(fontFileName).path() + "//" + item->text(0));
        QSize letterSize = svgWidget->renderer()->defaultSize();
        svgWidget->setFixedWidth(letterSize.width() * static_cast<qreal>(ui->treeWidget->height()) / letterSize.height());
    }
}

void FontDialog::deleteLetter()
{
    QChar letter = ui->choosenSymbolTextEdit->toPlainText().at(0);
    QTreeWidgetItem *letterItem = ui->treeWidget->findItems(letter, Qt::MatchCaseSensitive).first();
    delete ui->treeWidget->takeTopLevelItem(ui->treeWidget->indexOfTopLevelItem(letterItem));
    font.remove(letter);
}