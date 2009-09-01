/*
 ********************************************************************
    Copyright 2009 UCAR, NCAR, All Rights Reserved

    $LastChangedDate:  $

    $LastChangedRevision: $

    $LastChangedBy:  $

    $HeadURL: http://svn/svn/nidas/trunk/src/nidas/apps/config/configwindow.h $
 ********************************************************************
*/

#ifndef CONFIGWINDOW_H
#define CONFIGWINDOW_H

#include <QMainWindow>

#include <nidas/core/DSMSensor.h>
#include <nidas/core/XMLParser.h>
#include <nidas/core/Project.h>
#include <nidas/core/Site.h>
#include <nidas/core/PortSelectorTest.h>
#include <nidas/core/DSMConfig.h>

using namespace nidas::core;
namespace n_u = nidas::util;


#include "dsmtablewidget.h"

class QAction;
class QActionGroup;
class QLabel;
class QMenu;

class ConfigWindow : public QMainWindow
{
    Q_OBJECT

public:
    ConfigWindow();
    int parseFile(QString filename);

public slots:
    QString getFile();
    QString putFile();

private:
    QTabWidget *SiteTabs;
    void sensorTitle(DSMSensor * sensor, DSMTableWidget * DSMTable);
    void parseAnalog(const DSMConfig * dsm, DSMTableWidget * DSMTable);
    void parseOther(const DSMConfig * dsm, DSMTableWidget * DSMTable);

    const int numA2DChannels;

    xercesc::DOMDocument* doc;


class ConfigDOMErrorHandler : public xercesc::DOMErrorHandler {
 public:
    bool handleError(const xercesc::DOMError & domError) {
        cerr << domError.getMessage() << endl;
        return true;
    }
} errorHandler;


};
#endif

