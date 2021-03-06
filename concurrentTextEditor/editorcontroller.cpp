#include "editorcontroller.h"
#include <QKeyEvent>
#include <QRandomGenerator>
#include <QClipboard>
#include <QApplication>
#include <QTextBlock>
#include <QTimer>

EditorController::EditorController(QWidget *parent) : QTextEdit(parent)
{    
    _crdt = Crdt();

    //Set margins
    QTextDocument* doc = this->document();
    QTextBlockFormat blockFormat = doc->begin().blockFormat();
    blockFormat.setTopMargin(10);
    blockFormat.setLeftMargin(20);
    blockFormat.setRightMargin(20);
    QTextCursor{doc->end()}.setBlockFormat(blockFormat);
}

void EditorController::keyPressEvent(QKeyEvent *key)
{
    ensureCursorVisible();
    int pressed_key = key->key();
    int anchor = this->textCursor().anchor();
    QTextCursor temp = this->textCursor();
    temp.setPosition(anchor);
    QPair<int, int> anchorPosition = QPair<int,int>(temp.blockNumber(),temp.positionInBlock());
    QPair<int,int> cursorPosition = QPair<int,int>(this->textCursor().blockNumber(),this->textCursor().positionInBlock());
    //int deltaPositions = abs(cursorPosition - anchor);
    QPair<int,int> start, end, endBefore;
    Format currentFormat =_currentFormat;
    //get format
    QTextCharFormat charFormat;

    QString completeFilename = _crdt.getFileName();

    // Filter out not handled ket sequences
    if(!isKeySequenceHandled(key)){
        QTextEdit::keyPressEvent(key);
        return;
    }

    if(_shared == 1)
        completeFilename = _owner + "/" +  _crdt.getFileName();

    //take selection if there is one
    takeSelection(cursorPosition, anchorPosition, start, end);

    //ctrl-c handler to avoid "HeartBug"
    if(key->matches(QKeySequence::Copy) || pressed_key == Qt::Key_Copy){
        QTextEdit::keyPressEvent(key);
        return;
    }

    if(key->matches(QKeySequence::SelectAll)) {
        QTextEdit::keyPressEvent(key);
        return;
    }

    //ctrl-x handle to avoid "UpArrowBug"
    if(key->matches(QKeySequence::Cut) || pressed_key == Qt::Key_Cut) {
         //cancel the selection (if there is one)
        if(start!=end) {
            //Iterate over characters to be removed
            if(_crdt.calcBeforePosition(end, endBefore)) {
                deleteSelection(start, endBefore);
            }
        }
        QTextEdit::keyPressEvent(key);
        return;
    }

    //ctrl-v handler
    if(key->matches(QKeySequence::Paste) || pressed_key == Qt::Key_Paste) {

        QClipboard* clipboard = QApplication::clipboard();
        QString clipText = clipboard->text();
        if(clipText.isNull() || clipText.isEmpty()) {
            return;
        }
        if(start!=end) {
            if(_crdt.calcBeforePosition(end, endBefore)) {
                deleteSelection(start, endBefore);
            }
            //for insert we need to set the position to start, either it's out of range
            cursorPosition=start;
        }

        for(int writingIndex = 0; writingIndex <  clipText.length(); writingIndex++){
            //setCurrentFormat(charFormat, cursorPosition);
            QChar val = clipText[writingIndex];
            Format charF = Format::plain;
            _crdt.handleLocalInsert(val, cursorPosition, charF);
            emit broadcastEditWorker(completeFilename , _crdt._lastChar, _crdt._lastOperation, cursorPosition, _isPublic);
            if(clipText[writingIndex] == '\n' || clipText[writingIndex] == '\r' ) {
                cursorPosition.first++;
                cursorPosition.second = 0;
            } else {
                cursorPosition.second++;
            }
            setFormat(charFormat, charF);
        }
        this->textCursor().insertText(clipText,charFormat);

        return;
    }

    // Handle Char insert or return
    if( (pressed_key >= 0x20 && pressed_key <= 0x0ff && pressed_key != Qt::Key_Control) || pressed_key == Qt::Key_Return || pressed_key == Qt::Key_Tab) {

        setCurrentFormat(charFormat, cursorPosition);
        //cancel the selection (if there is one)
        if(start!=end) {
            if(_crdt.calcBeforePosition(end, endBefore)) {
                deleteSelection(start, endBefore);
            }
            cursorPosition=start;
        }

        _crdt.handleLocalInsert(key->text().data()[0], cursorPosition, currentFormat);
        this->textCursor().insertText(key->text().data()[0], charFormat);
        emit broadcastEditWorker(completeFilename , _crdt._lastChar, _crdt._lastOperation, cursorPosition, _isPublic);

        return;
    }

    // Handle selection deletion with backspace or delete key
    if((pressed_key == Qt::Key_Backspace || pressed_key == Qt::Key_Delete) && start!=end) {
        if(_crdt.calcBeforePosition(end, endBefore)) {
            deleteSelection(start, endBefore);
        }

    }

    // Handle backspace deletion
    if(pressed_key == Qt::Key_Backspace && start == end && (this->textCursor().position()-1) != -1) {

        QPair<int,int> startBefore;
        if(_crdt.calcBeforePosition(start, startBefore)) {
            QList<Char> chars = _crdt.handleLocalDelete(startBefore, startBefore);
            emit broadcastEditWorker(completeFilename , chars[0], EditType::deletion, startBefore, _isPublic);
        }
    }


    QTextCursor lastIndex = this->textCursor();
    lastIndex.movePosition(QTextCursor::End);

    // Handle "delete" deletion
    if(pressed_key == Qt::Key_Delete && this->textCursor() != lastIndex && start == end) {

        QPair<int,int> endAfter;
        if(_crdt.calcAfterPosition(end, endAfter)) {
            QList<Char> chars = _crdt.handleLocalDelete(endAfter,endAfter);
            emit broadcastEditWorker(completeFilename , chars[0], EditType::deletion, endAfter, _isPublic);
        }
    }

    // Let the editor do its thing on current text if no handler is found
    QTextEdit::keyPressEvent(key);
}

void EditorController::takeSelection(QPair<int,int> cursorPosition, QPair<int,int> anchorPosition, QPair<int,int> & startPos, QPair<int,int> & endPos) {
    if(cursorPosition.first == anchorPosition.first) {
        if(cursorPosition.second <= anchorPosition.second) {
            startPos = cursorPosition;
            endPos = anchorPosition;
        } else {
            startPos = anchorPosition;
            endPos = cursorPosition;
        }
    } else if(cursorPosition.first < anchorPosition.first) {
        startPos = cursorPosition;
        endPos = anchorPosition;
    } else {
        startPos = anchorPosition;
        endPos = cursorPosition;
    }
}

void EditorController::deleteSelection(QPair<int,int> start, QPair<int,int> end) {

    QString completeFilename = _crdt.getFileName();

    if(_shared)
        completeFilename = _owner + "/" + _crdt.getFileName();

    QList<Char> chars = _crdt.handleLocalDelete(start, end);
    foreach (Char c, chars) {
        emit broadcastEditWorker(completeFilename , c, EditType::deletion, QPair<int,int>(), _isPublic);
    }
}

QList<QPair<QString,Format>> EditorController::getRichClip(QPair<int,int> start, QPair<int,int> end) {
    QList<QPair<QString,Format>> richClip;
    if(start.first!=end.first) {
        richClip = _crdt.takeMultipleBufRows(start,end);
    } else {
        richClip = _crdt.takeSingleBufRow(start,end);
    }

    return richClip;
}

void EditorController::write(){

    QTextCharFormat format;
    QList<QList<QPair<QString, Format>>> textBuffer = _crdt.getTextBuffer();

    for (auto row : textBuffer) {
        for (auto pair: row) {
            setFormat(format, pair.second);
            this->textCursor().insertText(pair.first, format);
        }
    }
}

// Wrappers for crdt methods

QString EditorController::getFileName(){

    return _crdt.getFileName();
}

QUuid EditorController::getSiteID() {
    return _crdt.getSiteID();
}

bool EditorController::parseCteFile(QJsonDocument unparsedFile){
    return _crdt.parseCteFile(unparsedFile);
}

void EditorController::setUserColor(QString user, QColor color) {
    _usersColor.insert(user, color);
}

void EditorController::handleRemoteEdit(const QJsonObject &qjo) {

    QPair<int,int> position;
    QString user = qjo["username"].toString();
    QTextCursor editingCursor;
    QTextCursor cursorBeforeEdit;

    QTextCharFormat charFormat;
    charFormat.setBackground(_usersColor[user]);

    EditType edit = static_cast<EditType>(qjo["editType"].toInt());
    Format format = static_cast<Format>(qjo["content"].toObject()["format"].toInt());

    switch(edit) {

        case EditType::insertion:

            position = _crdt.handleRemoteInsert(qjo);
            editingCursor = this->textCursor();
            cursorBeforeEdit= this->textCursor();
            editingCursor.setPosition(_crdt.calcIndex(position));
            this->setTextCursor(editingCursor);
            //set format
            setFormat(charFormat, format);
            // Write
            this->textCursor().insertText(QString(qjo["content"].toObject()["value"].toString()[0]), charFormat);
            // Set cursor back to original position (before editing)
            this->setTextCursor(cursorBeforeEdit);


            break;

        case EditType::deletion:

            position = _crdt.handleRemoteDelete(qjo);
            editingCursor = this->textCursor();
            cursorBeforeEdit = this->textCursor();
            editingCursor.setPosition(_crdt.calcIndex(position) + 1);
            this->setTextCursor(editingCursor);
            this->textCursor().deletePreviousChar();
            this->setTextCursor(cursorBeforeEdit);

            break;

        case EditType::format:
            //handle format edit
            position = _crdt.handleRemoteFormat(qjo);
            editingCursor = this->textCursor();
            cursorBeforeEdit = this->textCursor();
            editingCursor.setPosition(_crdt.calcIndex(position));
            this->setTextCursor(editingCursor);
            setFormat(charFormat, format);
            editingCursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor);
            editingCursor.setCharFormat(charFormat);
            this->setTextCursor(cursorBeforeEdit);
            break;
        default:
            break;
    }
}

void EditorController::setFormat(QTextCharFormat &charFormat, Format format) {
    switch(format) {
        case Format::bold:
            charFormat.setFontWeight(QFont::Bold);
            charFormat.setFontUnderline(false);
            charFormat.setFontItalic(false);
            break;
        case Format::italics:
            charFormat.setFontWeight(QFont::Normal);
            charFormat.setFontUnderline(false);
            charFormat.setFontItalic(true);
            break;
        case Format::underline:
            charFormat.setFontWeight(QFont::Normal);
            charFormat.setFontUnderline(true);
            charFormat.setFontItalic(false);
            break;
        case Format::plain:
            charFormat.setFontWeight(QFont::Normal);
            charFormat.setFontUnderline(false);
            charFormat.setFontItalic(false);
            break;
        default:
            break;//se entro qua è finita
    }
}

void EditorController::setAccess(bool isPublic){

    _isPublic = isPublic;
}

Crdt EditorController::getCrdt() {
    return _crdt;
}

void EditorController::changeFormat(QPair<int,int> position, QPair<int,int> anchor, Format format) {
    if(_currentFormat == format) {
        _currentFormat = Format::plain;
    }
    else {
        _currentFormat = format;
    }
    QPair<int,int> start, end;

    QString completeFilename = _crdt.getFileName();

    if(_shared == 1)
        completeFilename = _owner + "/" +  _crdt.getFileName();

    //change format on the editor window
    //QTextCharFormat cursorFormat = this->textCursor().charFormat();
    QTextCharFormat cursorFormat;
    setFormat(cursorFormat,_currentFormat);
    this->textCursor().setCharFormat(cursorFormat);
    //change format on the file (no need to check if deltaPositions != 0 because editor checks if position != anchor
    takeSelection(position,anchor,start,end);
    if(start != end) {
        QList<Char> chars = _crdt.handleLocalFormat(start, end, _currentFormat);
        //change handlelocalformat getting the list of chars to broadcast as return value, then for cycle to broadcast
        foreach (Char c, chars) {
            emit broadcastEditWorker(completeFilename , c, EditType::format, QPair<int,int>(), _isPublic);
            //_crdt.calcBeforePosition(end, end);
        }
    }
}


void EditorController::setCurrentFormat(QTextCharFormat& charFormat, QPair<int,int> pos){

    Format currentFormat;
    pos.second -= 1;

//    if(this->textCursor().position() == 0) {
//        currentFormat = _currentFormat;
//    }
//    else {
//        currentFormat = _crdt.getCurrentFormat(pos); //forse la position.second va -1
//    }

    charFormat.setBackground(Qt::white);
    setFormat(charFormat, _currentFormat);
    this->textCursor().setCharFormat(charFormat);

}


bool EditorController::isKeySequenceHandled(QKeyEvent* key){

    int pressed_key = key->key();
    QString keyText = key->text();
    QFlags<Qt::KeyboardModifier> mod = key->modifiers();

    //conditions that are handled in keyPressEvent
    if(key->matches(QKeySequence::Copy) || key->matches(QKeySequence::Paste) || key->matches(QKeySequence::SelectAll)
            || key->matches(QKeySequence::Cut) || pressed_key == Qt::Key_Control || pressed_key == Qt::Key_Tab
            || pressed_key == Qt::Key_Control || pressed_key == Qt::Key_Return || pressed_key == Qt::Key_Delete
            || pressed_key == Qt::Key_Backspace || pressed_key == Qt::Key_Return || (pressed_key >= 0x20 && pressed_key <= 0x0ff && !mod.testFlag(Qt::ControlModifier))
            || pressed_key == Qt::Key_Paste || pressed_key == Qt::Key_Cut)
        return true;

    return false;
}
