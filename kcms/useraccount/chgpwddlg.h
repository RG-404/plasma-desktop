/**
 *  Copyright (C) 2015 Leslie Zhai <xiang.zhai@i-soft.com.cn>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 */

#ifndef __CHGPWDDLG_H__
#define __CHGPWDDLG_H__

#include <QWidget>

#include <QtAccountsService/AccountsManager>

class ChgPwdDlg : public QWidget 
{
    Q_OBJECT

public:
    explicit ChgPwdDlg(QtAccountsService::AccountsManager *am, 
                       QWidget *parent = NULL, 
                       Qt::WindowFlags f = Qt::Dialog);
    ~ChgPwdDlg();

private:
    QtAccountsService::AccountsManager *_am;
};

#endif /* __CHGPWDDLG_H__ */
