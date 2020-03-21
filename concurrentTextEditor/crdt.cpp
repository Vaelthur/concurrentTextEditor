#include "crdt.h"


Crdt::Crdt()
{
     _siteID = QUuid::createUuid();
}

Crdt::Crdt(QString siteID) {
    _siteID = QUuid(siteID);
}

bool Crdt::parseCteFile(QJsonDocument unparsedFile){
    _textBuffer = parseFile(unparsedFile);
    if(_textBuffer.isEmpty())
        return false;
    return true;
}


QList<QPair<QString, Format>> Crdt::parseFile(QJsonDocument unparsedFile){

    //costruzione della lista di Char
    _file.clear();
    QString buf;
    QJsonObject obj = unparsedFile.object();
    QString fileName = obj["requestedFiles"].toString();

    if(fileName.split("/").size() == 2) {
        _fileName = fileName.split("/")[1];
    } else {
        _fileName = fileName.split("/")[0];
    }

    QString fileContentString = unparsedFile["fileContent"].toString(); //string con tutto il content - QJsonValue

    QByteArray strBytes = fileContentString.toUtf8();

    QJsonDocument contentDoc = QJsonDocument::fromJson(strBytes);

    QJsonObject fileContentObj = contentDoc.object();

    QJsonArray arrayBuf = fileContentObj["content"].toArray();

    if(arrayBuf.empty())
        return QList<QPair<QString, Format>>();

    // Clear buffers
    _textBuffer.clear();
    _file.clear();

    foreach (const QJsonValue & tmpChar, arrayBuf) {
        Char c = getChar(tmpChar.toObject());

        //Find index for new char and insert it into _file Struct and _textBuffer
        int index = findInsertIndex(c);
        _file.insert(index, c);
        _textBuffer.insert(index, QPair<QString, Format>(c._value, c._format));
        //_textBuffer.insert(index, c._value);
    }

    return _textBuffer;
}

void Crdt::handleLocalInsert(QChar val, int index, Format format) {

    Char c = generateChar(val, index, format);
    insertChar(c, index);
    insertText(c._value, c._format, index);

    _lastChar = c;
    _lastOperation = EditType::insertion;
}

void Crdt::handleLocalDelete(int index) {

    Char c = _file.takeAt(index);
    _textBuffer.removeAt(index); //si può mettere anche lunghezza del blocco da eliminare IN AVANTI (per quando eliminiamo una selezione)

    _lastChar = c;
    _lastOperation = EditType::deletion;
}

void Crdt::handleLocalFormat(int index, Format format) {

    Char c = _file.at(index);
    c._format = format;
    replaceChar(c, index);

    _lastChar = c;
    _lastOperation = EditType::format;
}

void Crdt::replaceChar(Char val, int index) {

    _file.replace(index, val);
}

void Crdt::insertChar(Char c, int index) {

    _file.insert(index, c);    
}

void Crdt::insertText(QChar val, Format format, int index) {

    _textBuffer.insert(index, QPair<QString,Format>(val,format));
}

Char Crdt::generateChar(QChar val, int index, Format format) {

    QList<Identifier> posBefore;
    QList<Identifier> posAfter;
    QList<Identifier> newPos;
    if(index-1 >= 0)
        posBefore = _file.at(index - 1)._position;
    if(index < _file.length())
        posAfter = _file.at(index)._position;
    newPos = generatePosBetween(posBefore, posAfter, newPos);

    //TODO: version counter per globality
    //const localCounter = this.vector.localVersion.counter;

    return Char(val, 0/*to implement*/, _siteID, newPos, format);
}

int Crdt::retrieveStrategy(int level) {
  int strategy;

  switch (this->_strategy) {
    case 'plus':
      strategy = '+';
      break;
    case 'minus':
      strategy = '-';
      break;
    case 'random':
      //strategy = Math.round(Math.random()) === 0 ? '+' : '-';
      break;
    case 'every2nd':
      strategy = ((level+1) % 2) == 0 ? '-' : '+';
      break;
    case 'every3rd':
      strategy = ((level+1) % 3) == 0 ? '-' : '+';
      break;
    default:
      strategy = ((level+1) % 2) == 0 ? '-' : '+';
      break;
  }
  return strategy;
}

QList<Identifier> Crdt::generatePosBetween(QList<Identifier> posBefore, QList<Identifier> posAfter, QList<Identifier> newPos, int level) {

    int base = (int)qPow(_mult, level) * _base;
    int boundaryStrategy = retrieveStrategy(level);
    Identifier id1;
    Identifier id2;
    QList<Identifier> emptyAfter;

    if(posBefore.isEmpty())
        id1 = Identifier(0, _siteID);
    else
        id1 = posBefore[0];

    if(posAfter.isEmpty())
        id2 = Identifier(base, _siteID);
    else
        id2 = posAfter[0];

    if (id2._digit - id1._digit > 1) {
      int newDigit = generateIdBetween(id1._digit, id2._digit, boundaryStrategy);
      newPos.append(Identifier(newDigit, _siteID));
      return newPos;

    }
    else if (id2._digit - id1._digit == 1) {
      newPos.append(id1);
      return generatePosBetween(posBefore.mid(1), emptyAfter, newPos, level+1);
    }
    else if (id1._digit == id2._digit) {
      if (id1._siteID < id2._siteID) {
        newPos.append(id1);
        return generatePosBetween(posBefore.mid(1), emptyAfter, newPos, level+1);
      } else if (id1._siteID == id2._siteID) {
        newPos.append(id1);
        return generatePosBetween(posBefore.mid(1), posAfter.mid(1), newPos, level+1);
      } else {
        //implement exception
        //throw new Error("Fix Position Sorting");
      }
    }

    QList<Identifier> empty;
    return empty;
}

int Crdt::generateIdBetween(int min, int max, int boundaryStrategy) {

    if ((max - min) < _boundary) {
      min = min + 1;
    }
    else {
        if (boundaryStrategy == '-')
            min = max - _boundary;
        else {
            min = min + 1;
            max = min + _boundary;
        }
    }

    return qFloor(QRandomGenerator().bounded((double)1) * (max - min)) + min;
}

int Crdt::findInsertIndex(Char c) {

    int left = 0;
    int right = _file.length() - 1;
    int mid, compareNum;

    if (_file.length() == 0 || c.compareTo(_file.at(left)) < 0) {
      return left;
    } else if (c.compareTo(_file.at(right)) > 0) {
      return _file.length();
    }

    while (left + 1 < right) {
      mid = qFloor(left + (right - left) / 2);
      compareNum = c.compareTo(_file.at(mid));

      if (compareNum == 0) {
        return mid;
      } else if (compareNum > 0) {
        left = mid;
      } else {
        right = mid;
      }
    }

    return c.compareTo(_file.at(left)) == 0 ? left : right;

}

void Crdt::updateFileAtIndex(int index, Char c){
    _file.insert(index, c);
}


QString Crdt::getFileName(){

    if(_fileName.isNull() || _fileName.isEmpty()){
        //throw exception
    }

    return _fileName;
}

QList<QPair<QString, Format>> Crdt::getTextBuffer(){

    if(_textBuffer.isEmpty()){
        //throw exception
    }
    return _textBuffer;
}

QUuid Crdt::getSiteID() {
    return _siteID;
}


int Crdt::findIndexByPosition(Char c){

    int left = 0;
    int right = _file.length()- 1;
    int mid, compareNum;

    if (_file.length() == 0) {
          // throw exception
            return -1;
    }

    while (left + 1 < right) {
      mid = qFloor(left + (right - left) / 2);
      compareNum = c.compareTo(_file[mid]);

      if (compareNum == 0) {
        return mid;
      }
      else if (compareNum > 0) {
        left = mid;
      }
      else {
        right = mid;
      }
    }

    if (c.compareTo(_file[left]) == 0) {
      return left;
    }
    else if (c.compareTo(_file[right]) == 0) {
      return right;
    }
    else {
      // throw exception
        return -1;
    }
}

void Crdt::deleteChar(Char val, int index){
    _file.removeAt(index);
}

int Crdt::handleRemoteDelete(const QJsonObject &qjo) {

    Char c = getChar(qjo["content"].toObject());
    int index = findIndexByPosition(c);
    _file.removeAt(index);
    _textBuffer.removeAt(index);

    return index;
//    this.controller.deleteFromEditor(char.value, index, siteId);
//    this.deleteText(index);
}

int Crdt::handleRemoteInsert(const QJsonObject &qjo) {

    Char c = getChar(qjo["content"].toObject());
    int index = findInsertIndex(c);
    this->insertChar(c, index);
    _textBuffer.insert(index, QPair<QString,Format>(c._value,c._format));

    return index;
//  this.insertText(char.value, index);

//  this.controller.insertIntoEditor(char.value, index, char.siteId);

}

int Crdt::handleRemoteFormat(const QJsonObject &qjo) {
    Char c = getChar(qjo["content"].toObject());
    int index = findIndexByPosition(c);
    _file.replace(index, c);
    _textBuffer.replace(index, QPair<QString,Format>(c._value,c._format));
    //no need to modify textbuffer
    return index;
}

Char Crdt::getChar(QJsonObject jsonChar ){

    // Estrazione di Char da newChar JSonObject
    QChar val = jsonChar["value"].toString()[0];
    Format format = static_cast<Format>(jsonChar["format"].toInt());
    QUuid siteID = jsonChar["siteID"].toString();
    int counter = jsonChar["counter"].toInt();
    QJsonArray identifiers = jsonChar["position"].toArray();
    QList<Identifier> positions;

    foreach (const QJsonValue &tmpID, identifiers) {
        QJsonObject ID = tmpID.toObject();
        int digit = ID["digit"].toInt();
        QUuid oldSiteID = ID["siteID"].toString();
        Identifier identifier(digit,oldSiteID);
        positions.append(identifier);
    }

    return Char(val,counter,siteID,positions,format);
}


Format Crdt::getCurrentFormat(int index) {
    return _file.at(index)._format;
}
