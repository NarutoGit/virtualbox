/* $Id$ */
/** @file
 * VBox Qt GUI - UISnapshotPane class implementation.
 */

/*
 * Copyright (C) 2006-2016 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifdef VBOX_WITH_PRECOMPILED_HEADERS
# include <precomp.h>
#else  /* !VBOX_WITH_PRECOMPILED_HEADERS */

/* Qt includes: */
# include <QAccessibleWidget>
# include <QApplication>
# include <QDateTime>
# include <QHeaderView>
# include <QMenu>
# include <QPointer>
# include <QScrollBar>
# include <QWriteLocker>

/* GUI includes: */
# include "QITreeWidget.h"
# include "UIConverter.h"
# include "UIExtraDataManager.h"
# include "UIIconPool.h"
# include "UIMessageCenter.h"
# include "UIModalWindowManager.h"
# include "UISnapshotPane.h"
# include "UIToolBar.h"
# include "UIVirtualBoxEventHandler.h"
# include "UIWizardCloneVM.h"
# include "VBoxSnapshotDetailsDlg.h"
# include "VBoxTakeSnapshotDlg.h"

/* COM includes: */
# include "CConsole.h"

#endif /* !VBOX_WITH_PRECOMPILED_HEADERS */

/* Qt includes: */
#if QT_VERSION < 0x050000
# include <QWindowsStyle>
#endif /* QT_VERSION < 0x050000 */


/** QITreeWidgetItem subclass for snapshots items. */
class UISnapshotItem : public QITreeWidgetItem
{
    Q_OBJECT;

public:

    /** Casts QTreeWidgetItem* to UISnapshotItem* if possible. */
    static UISnapshotItem *toSnapshotItem(QTreeWidgetItem *pItem);
    /** Casts const QTreeWidgetItem* to const UISnapshotItem* if possible. */
    static const UISnapshotItem *toSnapshotItem(const QTreeWidgetItem *pItem);

    /** Constructs normal snapshot item (child of tree-widget). */
    UISnapshotItem(UISnapshotPane *pSnapshotWidget, QITreeWidget *pTreeWidget, const CSnapshot &comSnapshot);
    /** Constructs normal snapshot item (child of tree-widget-item). */
    UISnapshotItem(UISnapshotPane *pSnapshotWidget, QITreeWidgetItem *pRootItem, const CSnapshot &comSnapshot);

    /** Constructs "current state" item (child of tree-widget). */
    UISnapshotItem(UISnapshotPane *pSnapshotWidget, QITreeWidget *pTreeWidget, const CMachine &comMachine);
    /** Constructs "current state" item (child of tree-widget-item). */
    UISnapshotItem(UISnapshotPane *pSnapshotWidget, QITreeWidgetItem *pRootItem, const CMachine &comMachine);

    /** Returns item machine. */
    CMachine machine() const { return m_comMachine; }
    /** Returns item snapshot. */
    CSnapshot snapshot() const { return m_comSnapshot; }
    /** Returns item snapshot ID. */
    QString snapshotID() const { return m_strSnapshotID; }

    /** Returns item data for corresponding @a iColumn and @a iRole. */
    QVariant data(int iColumn, int iRole) const;

    /** Returns item text for corresponding @a iColumn. */
    QString text(int iColumn) const;

    /** Returns whether this is the "current state" item. */
    bool isCurrentStateItem() const;

    /** Calculates and returns the current item level. */
    int level() const;

    /** Returns whether the font is bold. */
    bool bold() const;
    /** Defines whether the font is @a fBold. */
    void setBold(bool fBold);

    /** Returns whether the font is italic. */
    bool italic() const;
    /** Defines whether the font is @a fItalic. */
    void setItalic(bool fItalic);

    /** Recaches the item's contents. */
    void recache();

    /** Returns current machine state. */
    KMachineState getCurrentState() const;

    /** Recaches current machine state. */
    void updateCurrentState(KMachineState enmState);

    /** Updates item age. */
    SnapshotAgeFormat updateAge();

protected:

    /** Adjusts item text. */
    void adjustText();

    /** Recaches item tool-tip. */
    void recacheToolTip();

private:

    /** Holds the pointer to the snapshot-widget this item belongs to. */
    QPointer<UISnapshotPane> m_pSnapshotWidget;

    /** Holds whether this is a "current state" item. */
    bool                     m_fCurrentState;

    /** Holds the snapshot COM wrapper. */
    CSnapshot                m_comSnapshot;
    /** Holds the machine COM wrapper. */
    CMachine                 m_comMachine;

    /** Holds the current snapshot ID. */
    QString                  m_strSnapshotID;
    /** Holds whether the current snapshot is online one. */
    bool                     m_fOnline;

    /** Holds the item description. */
    QString                  m_strDesc;
    /** Holds the item timestamp. */
    QDateTime                m_timestamp;

    /** Holds whether the current state is modified. */
    bool                     m_fCurrentStateModified;
    /** Holds the cached machine state. */
    KMachineState            m_enmMachineState;
};


/** QITreeWidget subclass for snapshots items. */
class UISnapshotTree : public QITreeWidget
{
    Q_OBJECT;

public:

    /** Constructs snapshot tree passing @a pParent to the base-class. */
    UISnapshotTree(QWidget *pParent);
};


/*********************************************************************************************************************************
*   Class UISnapshotItem implementation.                                                                                         *
*********************************************************************************************************************************/

/* static */
UISnapshotItem *UISnapshotItem::toSnapshotItem(QTreeWidgetItem *pItem)
{
    /* Get QITreeWidgetItem item first: */
    QITreeWidgetItem *pIItem = QITreeWidgetItem::toItem(pItem);
    if (!pIItem)
        return 0;

    /* Return casted UISnapshotItem then: */
    return qobject_cast<UISnapshotItem*>(pIItem);
}

/* static */
const UISnapshotItem *UISnapshotItem::toSnapshotItem(const QTreeWidgetItem *pItem)
{
    /* Get QITreeWidgetItem item first: */
    const QITreeWidgetItem *pIItem = QITreeWidgetItem::toItem(pItem);
    if (!pIItem)
        return 0;

    /* Return casted UISnapshotItem then: */
    return qobject_cast<const UISnapshotItem*>(pIItem);
}

UISnapshotItem::UISnapshotItem(UISnapshotPane *pSnapshotWidget, QITreeWidget *pTreeWidget, const CSnapshot &comSnapshot)
    : QITreeWidgetItem(pTreeWidget)
    , m_pSnapshotWidget(pSnapshotWidget)
    , m_fCurrentState(false)
    , m_comSnapshot(comSnapshot)
{
}

UISnapshotItem::UISnapshotItem(UISnapshotPane *pSnapshotWidget, QITreeWidgetItem *pRootItem, const CSnapshot &comSnapshot)
    : QITreeWidgetItem(pRootItem)
    , m_pSnapshotWidget(pSnapshotWidget)
    , m_fCurrentState(false)
    , m_comSnapshot(comSnapshot)
{
}

UISnapshotItem::UISnapshotItem(UISnapshotPane *pSnapshotWidget, QITreeWidget *pTreeWidget, const CMachine &comMachine)
    : QITreeWidgetItem(pTreeWidget)
    , m_pSnapshotWidget(pSnapshotWidget)
    , m_fCurrentState(true)
    , m_comMachine(comMachine)
{
    /* Fetch current machine state: */
    updateCurrentState(m_comMachine.GetState());
}

UISnapshotItem::UISnapshotItem(UISnapshotPane *pSnapshotWidget, QITreeWidgetItem *pRootItem, const CMachine &comMachine)
    : QITreeWidgetItem(pRootItem)
    , m_pSnapshotWidget(pSnapshotWidget)
    , m_fCurrentState(true)
    , m_comMachine(comMachine)
{
    /* Fetch current machine state: */
    updateCurrentState(m_comMachine.GetState());
}

QVariant UISnapshotItem::data(int iColumn, int iRole) const
{
    switch (iRole)
    {
        case Qt::DisplayRole:
        {
            /* Call to base-class for "current state" item, compose ourselves otherwise: */
            return m_fCurrentState ? QTreeWidgetItem::data(iColumn, iRole) :
                                     QString("%1%2")
                                         .arg(QTreeWidgetItem::data(iColumn, Qt::DisplayRole).toString())
                                         .arg(QTreeWidgetItem::data(iColumn, Qt::UserRole).toString());
        }
        case Qt::SizeHintRole:
        {
            /* Determine the icon metric: */
            const QStyle *pStyle = QApplication::style();
            const int iIconMetric = pStyle->pixelMetric(QStyle::PM_SmallIconSize);
            /* Determine the minimum size-hint for this tree-widget-item: */
            const QSize baseSizeHint = QTreeWidgetItem::data(iColumn, iRole).toSize();
            /* Determine the effective height-hint for this tree-widget-item: */
            const int iEffectiveHeightHint = qMax(baseSizeHint.height(),
                                                  iIconMetric + 2 * 2 /* margins */);
            /* Return size-hint for this tree-widget-item: */
            return QSize(baseSizeHint.width(), iEffectiveHeightHint);
        }
        default:
            break;
    }

    /* Call to base-class: */
    return QTreeWidgetItem::data(iColumn, iRole);
}

QString UISnapshotItem::text(int iColumn) const
{
    return QTreeWidgetItem::data(iColumn, Qt::DisplayRole).toString();
}

bool UISnapshotItem::isCurrentStateItem() const
{
    return m_comSnapshot.isNull();
}

int UISnapshotItem::level() const
{
    const QTreeWidgetItem *pItem = this;
    int iResult = 0;
    while (pItem->parent())
    {
        ++iResult;
        pItem = pItem->parent();
    }
    return iResult;
}

bool UISnapshotItem::bold() const
{
    return font(0).bold();
}

void UISnapshotItem::setBold(bool fBold)
{
    /* Update font: */
    QFont myFont = font(0);
    myFont.setBold(fBold);
    setFont(0, myFont);

    /* Adjust text: */
    adjustText();
}

bool UISnapshotItem::italic() const
{
    return font(0).italic();
}

void UISnapshotItem::setItalic(bool fItalic)
{
    /* Update font: */
    QFont myFont = font(0);
    myFont.setItalic(fItalic);
    setFont(0, myFont);

    /* Adjust text: */
    adjustText();
}

void UISnapshotItem::recache()
{
    /* For "current state" item: */
    if (m_fCurrentState)
    {
        /* Fetch machine information: */
        AssertReturnVoid(!m_comMachine.isNull());
        m_fCurrentStateModified = m_comMachine.GetCurrentStateModified();
        setText(0, m_fCurrentStateModified ?
                   UISnapshotPane::tr("Current State (changed)", "Current State (Modified)") :
                   UISnapshotPane::tr("Current State", "Current State (Unmodified)"));
        m_strDesc = m_fCurrentStateModified ?
                    UISnapshotPane::tr("The current state differs from the state stored in the current snapshot") :
                    QTreeWidgetItem::parent() != 0 ?
                    UISnapshotPane::tr("The current state is identical to the state stored in the current snapshot") :
                    QString();
    }
    /* For others: */
    else
    {
        /* Fetch snapshot information: */
        AssertReturnVoid(!m_comSnapshot.isNull());
        m_strSnapshotID = m_comSnapshot.GetId();
        setText(0, m_comSnapshot.GetName());
        m_fOnline = m_comSnapshot.GetOnline();
        setIcon(0, m_pSnapshotWidget->snapshotItemIcon(m_fOnline));
        m_strDesc = m_comSnapshot.GetDescription();
        m_timestamp.setTime_t(m_comSnapshot.GetTimeStamp() / 1000);
        m_fCurrentStateModified = false;
    }

    /* Adjust text: */
    adjustText();
    /* Update tool-tip: */
    recacheToolTip();
}

KMachineState UISnapshotItem::getCurrentState() const
{
    /* Make sure machine is valid: */
    if (m_comMachine.isNull())
        return KMachineState_Null;

    /* Return cached state: */
    return m_enmMachineState;
}

void UISnapshotItem::updateCurrentState(KMachineState enmState)
{
    /* Make sure machine is valid: */
    if (m_comMachine.isNull())
        return;

    /* Set corresponding icon: */
    setIcon(0, gpConverter->toIcon(enmState));
    /* Cache new state: */
    m_enmMachineState = enmState;
    /* Update timestamp: */
    m_timestamp.setTime_t(m_comMachine.GetLastStateChange() / 1000);
}

SnapshotAgeFormat UISnapshotItem::updateAge()
{
    /* Prepare age: */
    QString strAge;

    /* Age: [date time|%1d ago|%1h ago|%1min ago|%1sec ago] */
    SnapshotAgeFormat enmAgeFormat;
    const QDateTime now = QDateTime::currentDateTime();
    QDateTime then = m_timestamp;
    if (then > now)
        then = now; /* can happen if the host time is wrong */
    if (then.daysTo(now) > 30)
    {
        strAge = UISnapshotPane::tr(" (%1)").arg(then.toString(Qt::LocalDate));
        enmAgeFormat = SnapshotAgeFormat_Max;
    }
    else if (then.secsTo(now) > 60 * 60 * 24)
    {
        strAge = UISnapshotPane::tr(" (%1 ago)").arg(VBoxGlobal::daysToString(then.secsTo(now) / 60 / 60 / 24));
        enmAgeFormat = SnapshotAgeFormat_InDays;
    }
    else if (then.secsTo(now) > 60 * 60)
    {
        strAge = UISnapshotPane::tr(" (%1 ago)").arg(VBoxGlobal::hoursToString(then.secsTo(now) / 60 / 60));
        enmAgeFormat = SnapshotAgeFormat_InHours;
    }
    else if (then.secsTo(now) > 60)
    {
        strAge = UISnapshotPane::tr(" (%1 ago)").arg(VBoxGlobal::minutesToString(then.secsTo(now) / 60));
        enmAgeFormat = SnapshotAgeFormat_InMinutes;
    }
    else
    {
        strAge = UISnapshotPane::tr(" (%1 ago)").arg(VBoxGlobal::secondsToString(then.secsTo(now)));
        enmAgeFormat = SnapshotAgeFormat_InSeconds;
    }

    /* Update data: */
    setData(0, Qt::UserRole, strAge);

    /* Return age: */
    return enmAgeFormat;
}

void UISnapshotItem::adjustText()
{
    /* Make sure item is initialised: */
    if (!treeWidget())
        return;

    /* Calculate metrics: */
    QFontMetrics metrics(font(0));
    int iHei0 = (metrics.height() > 16 ?
                 metrics.height() /* text */ : 16 /* icon */) +
                2 * 2 /* 2 pixel per margin */;
    int iWid0 = metrics.width(text(0)) /* text */ +
                treeWidget()->indentation() /* indent */ +
                16 /* icon */;

    /* Adjust size finally: */
    setSizeHint(0, QSize(iWid0, iHei0));
}

void UISnapshotItem::recacheToolTip()
{
    /* Is the saved date today? */
    const bool fDateTimeToday = m_timestamp.date() == QDate::currentDate();

    /* Compose date time: */
    QString strDateTime = fDateTimeToday ?
                          m_timestamp.time().toString(Qt::LocalDate) :
                          m_timestamp.toString(Qt::LocalDate);

    /* Prepare details: */
    QString strDetails;

    /* For snapshot item: */
    if (!m_comSnapshot.isNull())
    {
        /* The current snapshot is always bold: */
        if (bold())
            strDetails = UISnapshotPane::tr(" (current, ", "Snapshot details");
        else
            strDetails = " (";

        /* Add online/offline information: */
        strDetails += m_fOnline ? UISnapshotPane::tr("online)", "Snapshot details")
                                : UISnapshotPane::tr("offline)", "Snapshot details");

        /* Add date/time information: */
        if (fDateTimeToday)
            strDateTime = UISnapshotPane::tr("Taken at %1", "Snapshot (time)").arg(strDateTime);
        else
            strDateTime = UISnapshotPane::tr("Taken on %1", "Snapshot (date + time)").arg(strDateTime);
    }
    /* For "current state" item: */
    else
    {
        strDateTime = UISnapshotPane::tr("%1 since %2", "Current State (time or date + time)")
                      .arg(gpConverter->toString(m_enmMachineState)).arg(strDateTime);
    }

    /* Prepare tool-tip: */
    QString strToolTip = QString("<nobr><b>%1</b>%2</nobr><br><nobr>%3</nobr>")
                             .arg(text(0)).arg(strDetails).arg(strDateTime);

    /* Append description if any: */
    if (!m_strDesc.isEmpty())
        strToolTip += "<hr>" + m_strDesc;

    /* Assign tool-tip finally: */
    setToolTip(0, strToolTip);
}


/*********************************************************************************************************************************
*   Class UISnapshotTree implementation.                                                                                         *
*********************************************************************************************************************************/

UISnapshotTree::UISnapshotTree(QWidget *pParent)
    : QITreeWidget(pParent)
{
    /* No header: */
    header()->hide();
    /* All columns as one: */
    setAllColumnsShowFocus(true);
    /* Our own context menu: */
    setContextMenuPolicy(Qt::CustomContextMenu);

#if QT_VERSION < 0x050000
    // WORKAROUND:
    // The snapshots widget is not very useful if there are a lot
    // of snapshots in a tree and the current Qt style decides not
    // to draw lines (branches) between the snapshot nodes; it is
    // then often unclear which snapshot is a child of another.
    // So on platforms whose styles do not normally draw branches,
    // we use QWindowsStyle which is present on every platform and
    // draws required thing like we want. */
// #if defined(RT_OS_DARWIN) || defined(RT_OS_LINUX) || defined(RT_OS_SOLARIS)
    QWindowsStyle *pTreeWidgetStyle = new QWindowsStyle;
    setStyle(pTreeWidgetStyle);
    connect(this, SIGNAL(destroyed(QObject *)), pTreeWidgetStyle, SLOT(deleteLater()));
// #endif
#endif /* QT_VERSION < 0x050000 */
}


/*********************************************************************************************************************************
*   Class UISnapshotPane implementation.                                                                                         *
*********************************************************************************************************************************/

UISnapshotPane::UISnapshotPane(QWidget *pParent)
    : QIWithRetranslateUI<QWidget>(pParent)
    , m_pCurrentSnapshotItem(0)
    , m_pSnapshotItemActionGroup(new QActionGroup(this))
    , m_pCurrentStateItemActionGroup(new QActionGroup(this))
    , m_pActionTakeSnapshot(new QAction(m_pCurrentStateItemActionGroup))
    , m_pActionRestoreSnapshot(new QAction(m_pSnapshotItemActionGroup))
    , m_pActionDeleteSnapshot(new QAction(m_pSnapshotItemActionGroup))
    , m_pActionShowSnapshotDetails(new QAction(m_pSnapshotItemActionGroup))
    , m_pActionCloneSnapshot(new QAction(m_pCurrentStateItemActionGroup))
    , m_fShapshotOperationsAllowed(false)
    , m_pSnapshotTree(0)
{
    /* Set contents margins: */
#if   defined(VBOX_WS_MAC)
    setContentsMargins(4, 5, 5, 5);
#elif defined(VBOX_WS_WIN)
    setContentsMargins(3, 5, 5, 0);
#elif defined(VBOX_WS_X11)
    setContentsMargins(0, 5, 5, 5);
#endif

    /* Cache pixmaps: */
    m_snapshotIconOffline = UIIconPool::iconSet(":/snapshot_offline_16px.png");
    m_snapshotIconOnline = UIIconPool::iconSet(":/snapshot_online_16px.png");

    /* Create VBox layout: */
    QVBoxLayout *pLayout = new QVBoxLayout(this);
    pLayout->setContentsMargins(0, 0, 0, 0);

    /* Determine icon metric: */
    const int iIconMetric = (int)(QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize) * 1.375);

    /* Create tool-bar: */
    UIToolBar *pToolBar = new UIToolBar(this);
    pToolBar->setIconSize(QSize(iIconMetric, iIconMetric));
    pToolBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    pToolBar->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    /* Add actions into tool-bar: */
    pToolBar->addAction(m_pActionTakeSnapshot);
    pToolBar->addSeparator();
    pToolBar->addAction(m_pActionRestoreSnapshot);
    pToolBar->addAction(m_pActionDeleteSnapshot);
    pToolBar->addAction(m_pActionShowSnapshotDetails);
    pToolBar->addSeparator();
    pToolBar->addAction(m_pActionCloneSnapshot);
    /* Add tool-bar into layout: */
    pLayout->addWidget(pToolBar);

    /* Setup action icons: */
    m_pActionTakeSnapshot->setIcon(UIIconPool::iconSetFull(
        ":/snapshot_take_22px.png", ":/snapshot_take_16px.png",
        ":/snapshot_take_disabled_22px.png", ":/snapshot_take_disabled_16px.png"));
    m_pActionRestoreSnapshot->setIcon(UIIconPool::iconSetFull(
        ":/snapshot_restore_22px.png", ":/snapshot_restore_16px.png",
        ":/snapshot_restore_disabled_22px.png", ":/snapshot_restore_disabled_16px.png"));
    m_pActionDeleteSnapshot->setIcon(UIIconPool::iconSetFull(
        ":/snapshot_delete_22px.png", ":/snapshot_delete_16px.png",
        ":/snapshot_delete_disabled_22px.png", ":/snapshot_delete_disabled_16px.png"));
    m_pActionShowSnapshotDetails->setIcon(UIIconPool::iconSetFull(
        ":/snapshot_show_details_22px.png", ":/snapshot_show_details_16px.png",
        ":/snapshot_show_details_disabled_22px.png", ":/snapshot_details_show_disabled_16px.png"));
    m_pActionCloneSnapshot->setIcon(UIIconPool::iconSetFull(
        ":/vm_clone_22px.png", ":/vm_clone_16px.png",
        ":/vm_clone_disabled_22px.png", ":/vm_clone_disabled_16px.png"));
    /* Setup action shortcuts: */
    m_pActionTakeSnapshot->setShortcut(QString("Ctrl+Shift+S"));
    m_pActionRestoreSnapshot->setShortcut(QString("Ctrl+Shift+R"));
    m_pActionDeleteSnapshot->setShortcut(QString("Ctrl+Shift+D"));
    m_pActionShowSnapshotDetails->setShortcut(QString("Ctrl+Space"));
    m_pActionCloneSnapshot->setShortcut(QString("Ctrl+Shift+C"));

    /* Create snapshot tree: */
    m_pSnapshotTree = new UISnapshotTree(this);
    /* Add snapshot tree into layout: */
    pLayout->addWidget(m_pSnapshotTree);

    /* Setup timer: */
    m_ageUpdateTimer.setSingleShot(true);

    /* Setup snapshot tree connections: */
    connect(m_pSnapshotTree, SIGNAL(currentItemChanged(QTreeWidgetItem *, QTreeWidgetItem *)),
            this, SLOT(sltCurrentItemChanged(QTreeWidgetItem *)));
    connect(m_pSnapshotTree, SIGNAL(customContextMenuRequested(const QPoint &)),
            this, SLOT(sltContextMenuRequested(const QPoint &)));
    connect(m_pSnapshotTree, SIGNAL(itemChanged(QTreeWidgetItem *, int)),
            this, SLOT(sltItemChanged(QTreeWidgetItem *)));
    connect(m_pSnapshotTree, SIGNAL(itemDoubleClicked(QTreeWidgetItem *, int)),
            this, SLOT(sltItemDoubleClicked(QTreeWidgetItem *)));
    /* Setup snapshot operation connections: */
    connect(m_pActionTakeSnapshot, SIGNAL(triggered()), this, SLOT(sltTakeSnapshot()));
    connect(m_pActionRestoreSnapshot, SIGNAL(triggered()), this, SLOT(sltRestoreSnapshot()));
    connect(m_pActionDeleteSnapshot, SIGNAL(triggered()), this, SLOT(sltDeleteSnapshot()));
    connect(m_pActionShowSnapshotDetails, SIGNAL(triggered()), this, SLOT(sltShowSnapshotDetails()));
    connect(m_pActionCloneSnapshot, SIGNAL(triggered()), this, SLOT(sltCloneSnapshot()));
    /* Setup Main event connections: */
    connect(gVBoxEvents, SIGNAL(sigMachineDataChange(QString)),
            this, SLOT(sltMachineDataChange(QString)));
    connect(gVBoxEvents, SIGNAL(sigMachineStateChange(QString, KMachineState)),
            this, SLOT(sltMachineStateChange(QString, KMachineState)));
    connect(gVBoxEvents, SIGNAL(sigSessionStateChange(QString, KSessionState)),
            this, SLOT(sltSessionStateChange(QString, KSessionState)));
    /* Setup timer event connections: */
    connect(&m_ageUpdateTimer, SIGNAL(timeout()), this, SLOT(sltUpdateSnapshotsAge()));

    /* Translate finally: */
    retranslateUi();
}

void UISnapshotPane::setMachine(const CMachine &comMachine)
{
    /* Cache passed machine: */
    m_comMachine = comMachine;

    /* Cache machine details: */
    if (m_comMachine.isNull())
    {
        m_strMachineID = QString();
        m_enmSessionState = KSessionState_Null;
        m_fShapshotOperationsAllowed = false;
    }
    else
    {
        m_strMachineID = comMachine.GetId();
        m_enmSessionState = comMachine.GetSessionState();
        m_fShapshotOperationsAllowed = gEDataManager->machineSnapshotOperationsEnabled(m_strMachineID);
    }

    /* Refresh everything: */
    refreshAll();
}

void UISnapshotPane::retranslateUi()
{
    /* Translate snapshot tree: */
    m_pSnapshotTree->setWhatsThis(tr("Contains snapshot tree of current virtual machine"));

    /* Translate actions names: */
    m_pActionTakeSnapshot->setText(tr("&Take..."));
    m_pActionRestoreSnapshot->setText(tr("&Restore"));
    m_pActionDeleteSnapshot->setText(tr("&Delete"));
    m_pActionShowSnapshotDetails->setText(tr("D&etails..."));
    m_pActionCloneSnapshot->setText(tr("&Clone..."));
    /* Translate actions tool-tips: */
    m_pActionTakeSnapshot->setToolTip(tr("Take Snapshot (%1)").arg(m_pActionTakeSnapshot->shortcut().toString()));
    m_pActionRestoreSnapshot->setToolTip(tr("Restore Snapshot (%1)").arg(m_pActionRestoreSnapshot->shortcut().toString()));
    m_pActionDeleteSnapshot->setToolTip(tr("Delete Snapshot (%1)").arg(m_pActionDeleteSnapshot->shortcut().toString()));
    m_pActionShowSnapshotDetails->setToolTip(tr("Show Snapshot Details (%1)").arg(m_pActionShowSnapshotDetails->shortcut().toString()));
    m_pActionCloneSnapshot->setToolTip(tr("Clone Virtual Machine (%1)").arg(m_pActionCloneSnapshot->shortcut().toString()));
    /* Translate actions status-tips: */
    m_pActionTakeSnapshot->setStatusTip(tr("Take a snapshot of the current virtual machine state"));
    m_pActionRestoreSnapshot->setStatusTip(tr("Restore selected snapshot of the virtual machine"));
    m_pActionDeleteSnapshot->setStatusTip(tr("Delete selected snapshot of the virtual machine"));
    m_pActionShowSnapshotDetails->setStatusTip(tr("Display a window with selected snapshot details"));
    m_pActionCloneSnapshot->setStatusTip(tr("Clone selected virtual machine"));
}

void UISnapshotPane::sltCurrentItemChanged(QTreeWidgetItem *pItem)
{
    /* Acquire corresponding snapshot item: */
    const UISnapshotItem *pSnapshotItem = UISnapshotItem::toSnapshotItem(pItem);

    /* Make the selected item visible: */
    if (pSnapshotItem)
    {
        m_pSnapshotTree->horizontalScrollBar()->setValue(0);
        m_pSnapshotTree->scrollToItem(pSnapshotItem);
        m_pSnapshotTree->horizontalScrollBar()->setValue(m_pSnapshotTree->indentation() * pSnapshotItem->level());
    }

    /* Check whether another direct session is open or not: */
    const bool fBusy = m_enmSessionState != KSessionState_Unlocked;

    /* Acquire machine state of the "current state" item: */
    KMachineState enmState = KMachineState_Null;
    if (currentStateItem())
        enmState = currentStateItem()->getCurrentState();

    /* Whether taking or deleting snapshots is possible right now: */
    const bool fCanTakeDeleteSnapshot =    !fBusy
                                        || enmState == KMachineState_PoweredOff
                                        || enmState == KMachineState_Saved
                                        || enmState == KMachineState_Aborted
                                        || enmState == KMachineState_Running
                                        || enmState == KMachineState_Paused;

    /* Enable/disable snapshot operations: */
    m_pActionTakeSnapshot->setEnabled(
           m_fShapshotOperationsAllowed
        && (   (   fCanTakeDeleteSnapshot
                && m_pCurrentSnapshotItem
                && pSnapshotItem
                && pSnapshotItem->isCurrentStateItem())
            || (   pSnapshotItem
                && !m_pCurrentSnapshotItem))
    );
    m_pActionRestoreSnapshot->setEnabled(
           !fBusy
        && m_pCurrentSnapshotItem
        && pSnapshotItem
        && !pSnapshotItem->isCurrentStateItem()
    );
    m_pActionDeleteSnapshot->setEnabled(
           m_fShapshotOperationsAllowed
        && fCanTakeDeleteSnapshot
        && m_pCurrentSnapshotItem
        && pSnapshotItem
        && !pSnapshotItem->isCurrentStateItem()
    );
    m_pActionShowSnapshotDetails->setEnabled(
           m_pCurrentSnapshotItem
        && pSnapshotItem
        && !pSnapshotItem->isCurrentStateItem()
    );
    m_pActionCloneSnapshot->setEnabled(
           pSnapshotItem
        && (   !pSnapshotItem->isCurrentStateItem()
            || !fBusy)
    );
}

void UISnapshotPane::sltContextMenuRequested(const QPoint &point)
{
    /* Search for corresponding item: */
    const QTreeWidgetItem *pItem = m_pSnapshotTree->itemAt(point);
    if (!pItem)
        return;

    /* Acquire corresponding snapshot item: */
    const UISnapshotItem *pSnapshotItem = UISnapshotItem::toSnapshotItem(pItem);
    AssertReturnVoid(pSnapshotItem);

    /* Prepare menu: */
    QMenu menu;
    /* For snapshot item: */
    if (m_pCurrentSnapshotItem && !pSnapshotItem->isCurrentStateItem())
    {
        menu.addAction(m_pActionRestoreSnapshot);
        menu.addAction(m_pActionDeleteSnapshot);
        menu.addAction(m_pActionShowSnapshotDetails);
        menu.addSeparator();
        menu.addAction(m_pActionCloneSnapshot);
    }
    /* For "current state" item: */
    else
    {
        menu.addAction(m_pActionTakeSnapshot);
        menu.addSeparator();
        menu.addAction(m_pActionCloneSnapshot);
    }

    /* Show menu: */
    menu.exec(m_pSnapshotTree->viewport()->mapToGlobal(point));
}

void UISnapshotPane::sltItemChanged(QTreeWidgetItem *pItem)
{
    /* Make sure nothing being edited in the meantime: */
    if (!m_lockReadWrite.tryLockForWrite())
        return;

    /* Acquire corresponding snapshot item: */
    const UISnapshotItem *pSnapshotItem = UISnapshotItem::toSnapshotItem(pItem);
    AssertReturnVoid(pSnapshotItem);

    /* Rename corresponding snapshot: */
    CSnapshot comSnapshot = pSnapshotItem->snapshotID().isNull() ? CSnapshot() : m_comMachine.FindSnapshot(pSnapshotItem->snapshotID());
    if (!comSnapshot.isNull() && comSnapshot.isOk() && comSnapshot.GetName() != pSnapshotItem->text(0))
        comSnapshot.SetName(pSnapshotItem->text(0));

    /* Allows editing again: */
    m_lockReadWrite.unlock();
}

void UISnapshotPane::sltItemDoubleClicked(QTreeWidgetItem *pItem)
{
    /* Acquire corresponding snapshot item: */
    const UISnapshotItem *pSnapshotItem = UISnapshotItem::toSnapshotItem(pItem);
    AssertReturnVoid(pSnapshotItem);

    /* Handle Ctrl+DoubleClick: */
    if (QApplication::keyboardModifiers() == Qt::ControlModifier)
    {
        /* As snapshot-restore procedure: */
        restoreSnapshot(true /* suppress non-critical warnings */);
    }
}

void UISnapshotPane::sltMachineDataChange(QString strMachineID)
{
    /* Make sure it's our VM: */
    if (strMachineID != m_strMachineID)
        return;

    /* Prevent snapshot editing in the meantime: */
    QWriteLocker locker(&m_lockReadWrite);

    /* Recache state current item: */
    currentStateItem()->recache();
}

void UISnapshotPane::sltMachineStateChange(QString strMachineID, KMachineState enmState)
{
    /* Make sure it's our VM: */
    if (strMachineID != m_strMachineID)
        return;

    /* Prevent snapshot editing in the meantime: */
    QWriteLocker locker(&m_lockReadWrite);

    /* Recache new machine state: */
    currentStateItem()->recache();
    currentStateItem()->updateCurrentState(enmState);
}

void UISnapshotPane::sltSessionStateChange(QString strMachineID, KSessionState enmState)
{
    /* Make sure it's our VM: */
    if (strMachineID != m_strMachineID)
        return;

    /* Prevent snapshot editing in the meantime: */
    QWriteLocker locker(&m_lockReadWrite);

    /* Recache new session state: */
    m_enmSessionState = enmState;
    sltCurrentItemChanged(m_pSnapshotTree->currentItem());
}

void UISnapshotPane::sltUpdateSnapshotsAge()
{
    /* Stop timer if active: */
    if (m_ageUpdateTimer.isActive())
        m_ageUpdateTimer.stop();

    /* Search for smallest snapshot age to optimize timer timeout: */
    const SnapshotAgeFormat age = traverseSnapshotAge(m_pSnapshotTree->invisibleRootItem());
    switch (age)
    {
        case SnapshotAgeFormat_InSeconds: m_ageUpdateTimer.setInterval(5 * 1000); break;
        case SnapshotAgeFormat_InMinutes: m_ageUpdateTimer.setInterval(60 * 1000); break;
        case SnapshotAgeFormat_InHours:   m_ageUpdateTimer.setInterval(60 * 60 * 1000); break;
        case SnapshotAgeFormat_InDays:    m_ageUpdateTimer.setInterval(24 * 60 * 60 * 1000); break;
        default:                          m_ageUpdateTimer.setInterval(0); break;
    }

    /* Restart timer if necessary: */
    if (m_ageUpdateTimer.interval() > 0)
        m_ageUpdateTimer.start();
}

bool UISnapshotPane::takeSnapshot()
{
    /* Simulate try-catch block: */
    bool fSuccess = false;
    do
    {
        /* Open a session (this call will handle all errors): */
        CSession comSession;
        if (m_enmSessionState != KSessionState_Unlocked)
            comSession = vboxGlobal().openExistingSession(m_strMachineID);
        else
            comSession = vboxGlobal().openSession(m_strMachineID);
        if (comSession.isNull())
            break;

        /* Simulate try-catch block: */
        do
        {
            /* Get corresponding machine object: */
            CMachine comMachine = comSession.GetMachine();

            /* Create take-snapshot dialog: */
            QWidget *pDlgParent = windowManager().realParentWindow(this);
            QPointer<VBoxTakeSnapshotDlg> pDlg = new VBoxTakeSnapshotDlg(pDlgParent, m_comMachine);
            windowManager().registerNewParent(pDlg, pDlgParent);

            // TODO: Assign corresponding icon through sub-dialog API: */
            QPixmap pixmap = vboxGlobal().vmUserPixmapDefault(m_comMachine);
            if (pixmap.isNull())
                pixmap = vboxGlobal().vmGuestOSTypePixmapDefault(m_comMachine.GetOSTypeId());
            pDlg->mLbIcon->setPixmap(pixmap);

            /* Search for the max available snapshot index: */
            int iMaxSnapShotIndex = 0;
            QString strSnapshotName = tr("Snapshot %1");
            QRegExp regExp(QString("^") + strSnapshotName.arg("([0-9]+)") + QString("$"));
            QTreeWidgetItemIterator iterator(m_pSnapshotTree);
            while (*iterator)
            {
                QString strSnapshot = static_cast<UISnapshotItem*>(*iterator)->text(0);
                int iPos = regExp.indexIn(strSnapshot);
                if (iPos != -1)
                    iMaxSnapShotIndex = regExp.cap(1).toInt() > iMaxSnapShotIndex ? regExp.cap(1).toInt() : iMaxSnapShotIndex;
                ++iterator;
            }
            // TODO: Assign corresponding snapshot name through sub-dialog API: */
            pDlg->mLeName->setText(strSnapshotName.arg(iMaxSnapShotIndex + 1));

            /* Show Take Snapshot dialog: */
            if (pDlg->exec() != QDialog::Accepted)
            {
                /* Cleanup dialog if it wasn't destroyed in own loop: */
                if (pDlg)
                    delete pDlg;
                break;
            }

            /* Acquire real snapshot name/description: */
            const QString strRealSnapshotName = pDlg->mLeName->text().trimmed();
            const QString strRealSnapshotDescription = pDlg->mTeDescription->toPlainText();

            /* Cleanup dialog: */
            delete pDlg;

            /* Take snapshot: */
            QString strSnapshotID;
            CProgress comProgress = comMachine.TakeSnapshot(strRealSnapshotName, strRealSnapshotDescription, true, strSnapshotID);
            if (!comMachine.isOk())
            {
                msgCenter().cannotTakeSnapshot(comMachine, m_comMachine.GetName());
                break;
            }

            /* Show snapshot taking progress: */
            msgCenter().showModalProgressDialog(comProgress, m_comMachine.GetName(), ":/progress_snapshot_create_90px.png");
            if (!comProgress.isOk() || comProgress.GetResultCode() != 0)
            {
                msgCenter().cannotTakeSnapshot(comProgress, m_comMachine.GetName());
                break;
            }

            /* Mark snapshot restoring successful: */
            fSuccess = true;
        }
        while (0);

        /* Cleanup try-catch block: */
        comSession.UnlockMachine();
    }
    while (0);

    /* Return result: */
    return fSuccess;
}

bool UISnapshotPane::restoreSnapshot(bool fSuppressNonCriticalWarnings /* = false */)
{
    /* Simulate try-catch block: */
    bool fSuccess = false;
    do
    {
        /* Acquire currently chosen snapshot item: */
        const UISnapshotItem *pSnapshotItem = UISnapshotItem::toSnapshotItem(m_pSnapshotTree->currentItem());
        AssertPtr(pSnapshotItem);
        if (!pSnapshotItem)
            break;

        /* Get corresponding snapshot: */
        const CSnapshot comSnapshot = pSnapshotItem->snapshot();
        Assert(!comSnapshot.isNull());
        if (comSnapshot.isNull())
            break;

        /* If non-critical warnings are not hidden or current state is changed: */
        if (!fSuppressNonCriticalWarnings || m_comMachine.GetCurrentStateModified())
        {
            /* Ask if user really wants to restore the selected snapshot: */
            int iResultCode = msgCenter().confirmSnapshotRestoring(comSnapshot.GetName(), m_comMachine.GetCurrentStateModified());
            if (iResultCode & AlertButton_Cancel)
                break;

            /* Ask if user also wants to create new snapshot of current state which is changed: */
            if (iResultCode & AlertOption_CheckBox)
            {
                /* Take snapshot of changed current state: */
                m_pSnapshotTree->setCurrentItem(currentStateItem());
                if (!takeSnapshot())
                    break;
            }
        }

        /* Open a direct session (this call will handle all errors): */
        CSession comSession = vboxGlobal().openSession(m_strMachineID);
        if (comSession.isNull())
            break;

        /* Simulate try-catch block: */
        do
        {
            /* Restore chosen snapshot: */
            CMachine comMachine = comSession.GetMachine();
            CProgress comProgress = comMachine.RestoreSnapshot(comSnapshot);
            if (!comMachine.isOk())
            {
                msgCenter().cannotRestoreSnapshot(comMachine, comSnapshot.GetName(), m_comMachine.GetName());
                break;
            }

            /* Show snapshot restoring progress: */
            msgCenter().showModalProgressDialog(comProgress, m_comMachine.GetName(), ":/progress_snapshot_restore_90px.png");
            if (!comProgress.isOk() || comProgress.GetResultCode() != 0)
            {
                msgCenter().cannotRestoreSnapshot(comProgress, comSnapshot.GetName(), m_comMachine.GetName());
                break;
            }

            /* Mark snapshot restoring successful: */
            fSuccess = true;
        }
        while (0);

        /* Cleanup try-catch block: */
        comSession.UnlockMachine();
    }
    while (0);

    /* Return result: */
    return fSuccess;
}

bool UISnapshotPane::deleteSnapshot()
{
    /* Simulate try-catch block: */
    bool fSuccess = false;
    do
    {
        /* Acquire currently chosen snapshot item: */
        const UISnapshotItem *pSnapshotItem = UISnapshotItem::toSnapshotItem(m_pSnapshotTree->currentItem());
        AssertPtr(pSnapshotItem);
        if (!pSnapshotItem)
            break;

        /* Get corresponding snapshot: */
        const CSnapshot comSnapshot = pSnapshotItem->snapshot();
        Assert(!comSnapshot.isNull());
        if (comSnapshot.isNull())
            break;

        /* Ask if user really wants to remove the selected snapshot: */
        if (!msgCenter().confirmSnapshotRemoval(comSnapshot.GetName()))
            break;

        /** @todo check available space on the target filesystem etc etc. */
#if 0
        if (!msgCenter().warnAboutSnapshotRemovalFreeSpace(comSnapshot.GetName(),
                                                           "/home/juser/.VirtualBox/Machines/SampleVM/Snapshots/{01020304-0102-0102-0102-010203040506}.vdi",
                                                           "59 GiB",
                                                           "15 GiB"))
            break;
#endif

        /* Open a session (this call will handle all errors): */
        CSession comSession;
        if (m_enmSessionState != KSessionState_Unlocked)
            comSession = vboxGlobal().openExistingSession(m_strMachineID);
        else
            comSession = vboxGlobal().openSession(m_strMachineID);
        if (comSession.isNull())
            break;

        /* Simulate try-catch block: */
        do
        {
            /* Remove chosen snapshot: */
            CMachine comMachine = comSession.GetMachine();
            CProgress comProgress = comMachine.DeleteSnapshot(pSnapshotItem->snapshotID());
            if (!comMachine.isOk())
            {
                msgCenter().cannotRemoveSnapshot(comMachine,  comSnapshot.GetName(), m_comMachine.GetName());
                break;
            }

            /* Show snapshot removing progress: */
            msgCenter().showModalProgressDialog(comProgress, m_comMachine.GetName(), ":/progress_snapshot_discard_90px.png");
            if (!comProgress.isOk() || comProgress.GetResultCode() != 0)
            {
                msgCenter().cannotRemoveSnapshot(comProgress,  comSnapshot.GetName(), m_comMachine.GetName());
                break;
            }

            /* Mark snapshot removing successful: */
            fSuccess = true;
        }
        while (0);

        /* Cleanup try-catch block: */
        comSession.UnlockMachine();
    }
    while (0);

    /* Return result: */
    return fSuccess;
}

void UISnapshotPane::showSnapshotDetails()
{
    /* Acquire currently chosen snapshot item: */
    const UISnapshotItem *pSnapshotItem = UISnapshotItem::toSnapshotItem(m_pSnapshotTree->currentItem());
    AssertReturnVoid(pSnapshotItem);

    /* Get corresponding snapshot: */
    const CSnapshot comSnapshot = pSnapshotItem->snapshot();
    AssertReturnVoid(!comSnapshot.isNull());

    /* Show Snapshot Details dialog: */
    QPointer<VBoxSnapshotDetailsDlg> pDlg = new VBoxSnapshotDetailsDlg(this);
    pDlg->getFromSnapshot(comSnapshot);
    if (pDlg->exec() == QDialog::Accepted)
        pDlg->putBackToSnapshot();
    if (pDlg)
        delete pDlg;
}

void UISnapshotPane::cloneSnapshot()
{
    /* Acquire currently chosen snapshot item: */
    const UISnapshotItem *pSnapshotItem = UISnapshotItem::toSnapshotItem(m_pSnapshotTree->currentItem());
    AssertReturnVoid(pSnapshotItem);

    /* Get desired machine/snapshot: */
    CMachine comMachine;
    CSnapshot comSnapshot;
    if (pSnapshotItem->isCurrentStateItem())
        comMachine = pSnapshotItem->machine();
    else
    {
        comSnapshot = pSnapshotItem->snapshot();
        AssertReturnVoid(!comSnapshot.isNull());
        comMachine = comSnapshot.GetMachine();
    }
    AssertReturnVoid(!comMachine.isNull());

    /* Show Clone VM wizard: */
    UISafePointerWizard pWizard = new UIWizardCloneVM(this, comMachine, comSnapshot);
    pWizard->prepare();
    pWizard->exec();
    if (pWizard)
        delete pWizard;
}

void UISnapshotPane::refreshAll()
{
    /* Prevent snapshot editing in the meantime: */
    QWriteLocker locker(&m_lockReadWrite);

    /* If VM is null, just updated the current itm: */
    if (m_comMachine.isNull())
    {
        sltCurrentItemChanged();
        return;
    }

    /* Remember the selected item and it's first child: */
    QString strSelectedItem, strFirstChildOfSelectedItem;
    const UISnapshotItem *pSnapshotItem = UISnapshotItem::toSnapshotItem(m_pSnapshotTree->currentItem());
    if (pSnapshotItem)
    {
        strSelectedItem = pSnapshotItem->snapshotID();
        if (pSnapshotItem->child(0))
            strFirstChildOfSelectedItem = UISnapshotItem::toSnapshotItem(pSnapshotItem->child(0))->snapshotID();
    }

    /* Clear the tree: */
    m_pSnapshotTree->clear();

    /* If machine has snapshots: */
    if (m_comMachine.GetSnapshotCount() > 0)
    {
        /* Get the first snapshot: */
        const CSnapshot comSnapshot = m_comMachine.FindSnapshot(QString());

        /* Populate snapshot tree: */
        populateSnapshots(comSnapshot, 0);
        /* And make sure it has current snapshot item: */
        Assert(m_pCurrentSnapshotItem);

        /* Add the "current state" item as a child to current snapshot item: */
        UISnapshotItem *pCsi = new UISnapshotItem(this, m_pCurrentSnapshotItem, m_comMachine);
        pCsi->setBold(true);
        pCsi->recache();

        /* Search for a previously selected item: */
        UISnapshotItem *pCurrentItem = findItem(strSelectedItem);
        if (pCurrentItem == 0)
            pCurrentItem = findItem(strFirstChildOfSelectedItem);
        if (pCurrentItem == 0)
            pCurrentItem = currentStateItem();

        /* Choose current item: */
        m_pSnapshotTree->scrollToItem(pCurrentItem);
        m_pSnapshotTree->setCurrentItem(pCurrentItem);
        sltCurrentItemChanged(pCurrentItem);
    }
    /* If machine has no snapshots: */
    else
    {
        /* There is no current snapshot item: */
        m_pCurrentSnapshotItem = 0;

        /* Add the "current state" item as a child of snapshot tree: */
        UISnapshotItem *pCsi = new UISnapshotItem(this, m_pSnapshotTree, m_comMachine);
        pCsi->setBold(true);
        pCsi->recache();

        /* Choose current item: */
        m_pSnapshotTree->setCurrentItem(pCsi);
        sltCurrentItemChanged(pCsi);
    }

    /* Update age: */
    sltUpdateSnapshotsAge();

    /* Adjust snapshot tree: */
    m_pSnapshotTree->resizeColumnToContents(0);
}

void UISnapshotPane::populateSnapshots(const CSnapshot &comSnapshot, QITreeWidgetItem *pItem)
{
    /* Create a child of passed item: */
    UISnapshotItem *pSnapshotItem = pItem ? new UISnapshotItem(this, pItem, comSnapshot) :
                                            new UISnapshotItem(this, m_pSnapshotTree, comSnapshot);
    /* And recache it's content: */
    pSnapshotItem->recache();

    /* Mark current snapshot item bold and remember it: */
    CSnapshot comCurrentSnapshot = m_comMachine.GetCurrentSnapshot();
    if (!comCurrentSnapshot.isNull() && comCurrentSnapshot.GetId() == comSnapshot.GetId())
    {
        pSnapshotItem->setBold(true);
        m_pCurrentSnapshotItem = pSnapshotItem;
    }

    /* Walk through the children recursively: */
    foreach (const CSnapshot &comIteratedSnapshot, comSnapshot.GetChildren())
        populateSnapshots(comIteratedSnapshot, pSnapshotItem);

    /* Expand the newly created item: */
    pSnapshotItem->setExpanded(true);
    /* And mark it as editable: */
    pSnapshotItem->setFlags(pSnapshotItem->flags() | Qt::ItemIsEditable);
}

UISnapshotItem *UISnapshotPane::findItem(const QString &strSnapshotID) const
{
    /* Search for the first item with required ID: */
    QTreeWidgetItemIterator it(m_pSnapshotTree);
    while (*it)
    {
        UISnapshotItem *pSnapshotItem = UISnapshotItem::toSnapshotItem(*it);
        if (pSnapshotItem->snapshotID() == strSnapshotID)
            return pSnapshotItem;
        ++it;
    }

    /* Null by default: */
    return 0;
}

UISnapshotItem *UISnapshotPane::currentStateItem() const
{
    /* Last child of the current snapshot item if any or first child of invisible root item otherwise: */
    QTreeWidgetItem *pCsi = m_pCurrentSnapshotItem ?
                            m_pCurrentSnapshotItem->child(m_pCurrentSnapshotItem->childCount() - 1) :
                            m_pSnapshotTree->invisibleRootItem()->child(0);
    return static_cast<UISnapshotItem*>(pCsi);
}

SnapshotAgeFormat UISnapshotPane::traverseSnapshotAge(QTreeWidgetItem *pItem) const
{
    /* Acquire corresponding snapshot item: */
    UISnapshotItem *pSnapshotItem = UISnapshotItem::toSnapshotItem(pItem);

    /* Fetch the snapshot age of the root if it's valid: */
    SnapshotAgeFormat age = pSnapshotItem ? pSnapshotItem->updateAge() : SnapshotAgeFormat_Max;

    /* Walk through the children recursively: */
    for (int i = 0; i < pItem->childCount(); ++i)
    {
        /* Fetch the smallest snapshot age of the children: */
        const SnapshotAgeFormat newAge = traverseSnapshotAge(pItem->child(i));
        /* Remember the smallest snapshot age among existing: */
        age = newAge < age ? newAge : age;
    }

    /* Return result: */
    return age;
}

#include "UISnapshotPane.moc"

