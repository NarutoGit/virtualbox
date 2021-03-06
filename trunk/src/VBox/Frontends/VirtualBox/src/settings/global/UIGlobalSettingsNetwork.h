/* $Id$ */
/** @file
 * VBox Qt GUI - UIGlobalSettingsNetwork class declaration.
 */

/*
 * Copyright (C) 2009-2017 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef ___UIGlobalSettingsNetwork_h___
#define ___UIGlobalSettingsNetwork_h___

/* GUI includes: */
#include "UISettingsPage.h"
#include "UIGlobalSettingsNetwork.gen.h"
#include "UIPortForwardingTable.h"

/* Forward declarations: */
class UIItemNetworkNAT;
class UIItemNetworkHost;
struct UIDataSettingsGlobalNetwork;
struct UIDataSettingsGlobalNetworkNAT;
struct UIDataSettingsGlobalNetworkHost;
typedef UISettingsCache<UIDataPortForwardingRule> UISettingsCachePortForwardingRule;
typedef UISettingsCachePoolOfTwo<UIDataSettingsGlobalNetworkNAT, UISettingsCachePortForwardingRule, UISettingsCachePortForwardingRule> UISettingsCacheGlobalNetworkNAT;
typedef UISettingsCache<UIDataSettingsGlobalNetworkHost> UISettingsCacheGlobalNetworkHost;
typedef UISettingsCachePoolOfTwo<UIDataSettingsGlobalNetwork, UISettingsCacheGlobalNetworkNAT, UISettingsCacheGlobalNetworkHost> UISettingsCacheGlobalNetwork;


/** Global settings: Network page. */
class UIGlobalSettingsNetwork : public UISettingsPageGlobal,
                                public Ui::UIGlobalSettingsNetwork
{
    Q_OBJECT;

public:

    /** Constructs Network settings page. */
    UIGlobalSettingsNetwork();
    /** Destructs Network settings page. */
    ~UIGlobalSettingsNetwork();

protected:

    /** Loads data into the cache from corresponding external object(s),
      * this task COULD be performed in other than the GUI thread. */
    virtual void loadToCacheFrom(QVariant &data) /* override */;
    /** Loads data into corresponding widgets from the cache,
      * this task SHOULD be performed in the GUI thread only. */
    virtual void getFromCache() /* override */;

    /** Saves data from corresponding widgets to the cache,
      * this task SHOULD be performed in the GUI thread only. */
    virtual void putToCache() /* override */;
    /** Saves data from the cache to corresponding external object(s),
      * this task COULD be performed in other than the GUI thread. */
    virtual void saveFromCacheTo(QVariant &data) /* overrride */;

    /** Performs validation, updates @a messages list if something is wrong. */
    virtual bool validate(QList<UIValidationMessage> &messages) /* override */;

    /** Defines TAB order for passed @a pWidget. */
    virtual void setOrderAfter(QWidget *pWidget) /* override */;

    /** Handles translation event. */
    virtual void retranslateUi() /* override */;

private slots:

    /** Handles command to add NAT network. */
    void sltAddNetworkNAT();
    /** Handles command to edit NAT network. */
    void sltEditNetworkNAT();
    /** Handles command to remove NAT network. */
    void sltRemoveNetworkNAT();
    /** Handles @a pChangedItem change for NAT network tree. */
    void sltHandleItemChangeNetworkNAT(QTreeWidgetItem *pChangedItem);
    /** Handles NAT network tree current item change. */
    void sltHandleCurrentItemChangeNetworkNAT();
    /** Handles context menu request for @a position of NAT network tree. */
    void sltHandleContextMenuRequestNetworkNAT(const QPoint &position);

    /** Handles command to add host network. */
    void sltAddNetworkHost();
    /** Handles command to edit host network. */
    void sltEditNetworkHost();
    /** Handles command to remove host network. */
    void sltRemoveNetworkHost();
    /** Handles host network tree current item change. */
    void sltHandleCurrentItemChangeNetworkHost();
    /** Handles context menu request for @a position of host network tree. */
    void sltHandleContextMenuRequestNetworkHost(const QPoint &position);

private:

    /** Prepares all. */
    void prepare();
    /** Prepares 'NAT Network' tab. */
    void prepareTabNAT();
    /** Prepares 'Host Network' tab. */
    void prepareTabHost();
    /** Prepares connections. */
    void prepareConnections();
    /** Cleanups all. */
    void cleanup();

    /** Saves existing network data from the cache. */
    bool saveNetworkData();

    /** Uploads NAT @a network data into passed @a cache storage unit. */
    void loadToCacheFromNetworkNAT(const CNATNetwork &network, UISettingsCacheGlobalNetworkNAT &cache);
    /** Removes corresponding NAT network on the basis of @a cache. */
    bool removeNetworkNAT(const UISettingsCacheGlobalNetworkNAT &cache);
    /** Creates corresponding NAT network on the basis of @a cache. */
    bool createNetworkNAT(const UISettingsCacheGlobalNetworkNAT &cache);
    /** Updates @a cache of corresponding NAT network. */
    bool updateNetworkNAT(const UISettingsCacheGlobalNetworkNAT &cache);
    /** Creates a new item in the NAT network tree on the basis of passed @a cache. */
    void createTreeWidgetItemForNetworkNAT(const UISettingsCacheGlobalNetworkNAT &cache);
    /** Creates a new item in the NAT network tree on the basis of passed @a data, @a ipv4rules, @a ipv6rules, @a fChooseItem if requested. */
    void createTreeWidgetItemForNetworkNAT(const UIDataSettingsGlobalNetworkNAT &data,
                                           const UIPortForwardingDataList &ipv4rules,
                                           const UIPortForwardingDataList &ipv6rules,
                                           bool fChooseItem = false);
    /** Removes existing @a pItem from the NAT network tree. */
    void removeTreeWidgetItemOfNetworkNAT(UIItemNetworkNAT *pItem);
    /** Returns whether the NAT network described by the @a cache could be updated or recreated otherwise. */
    bool isNetworkCouldBeUpdated(const UISettingsCacheGlobalNetworkNAT &cache) const;

    /** Uploads host @a network data into passed @a data storage unit. */
    void loadToCacheFromNetworkHost(const CHostNetworkInterface &iface, UISettingsCacheGlobalNetworkHost &cache);
    /** Saves @a data to corresponding host network. */
    bool saveDataNetworkHost(const UISettingsCacheGlobalNetworkHost &cache);
    /** Creates a new item in the host network tree on the basis of passed @a data, @a fChooseItem if requested. */
    void createTreeWidgetItemForNetworkHost(const UISettingsCacheGlobalNetworkHost &cache, bool fChooseItem = false);
    /** Removes existing @a pItem from the host network tree. */
    void removeTreeWidgetItemOfNetworkHost(UIItemNetworkHost *pItem);

    /** Holds the Add NAT network action instance. */
    QAction *m_pActionAddNetworkNAT;
    /** Holds the Edit NAT network action instance. */
    QAction *m_pActionEditNetworkNAT;
    /** Holds the Remove NAT network action instance. */
    QAction *m_pActionRemoveNetworkNAT;

    /** Holds the Add host network action instance. */
    QAction *m_pActionAddNetworkHost;
    /** Holds the Edit host network action instance. */
    QAction *m_pActionEditNetworkHost;
    /** Holds the Remove host network action instance. */
    QAction *m_pActionRemoveNetworkHost;

    /** Holds the page data cache instance. */
    UISettingsCacheGlobalNetwork *m_pCache;
};

#endif /* !___UIGlobalSettingsNetwork_h___ */

