/****************************************************************************
 **
 ** Copyright (C) Qxt Foundation. Some rights reserved.
 **
 ** This file is part of the QxtGui module of the Qxt library.
 **
 ** This library is free software; you can redistribute it and/or modify it
 ** under the terms of the Common Public License, version 1.0, as published
 ** by IBM, and/or under the terms of the GNU Lesser General Public License,
 ** version 2.1, as published by the Free Software Foundation.
 **
 ** This file is provided "AS IS", without WARRANTIES OR CONDITIONS OF ANY
 ** KIND, EITHER EXPRESS OR IMPLIED INCLUDING, WITHOUT LIMITATION, ANY
 ** WARRANTIES OR CONDITIONS OF TITLE, NON-INFRINGEMENT, MERCHANTABILITY OR
 ** FITNESS FOR A PARTICULAR PURPOSE.
 **
 ** You should have received a copy of the CPL and the LGPL along with this
 ** file. See the LICENSE file and the cpl1.0.txt/lgpl-2.1.txt files
 ** included with the source distribution for more information.
 ** If you did not receive a copy of the licenses, contact the Qxt Foundation.
 **
 ** <http://libqxt.org>  <foundation@libqxt.org>
 **
 ****************************************************************************/
#include "qxtconfigdialog_p.h"
#include "qxtconfigdialog.h"
#include "qxtconfigwidget.h"
#if QT_VERSION >= 0x040200
#include <QDialogButtonBox>
#else // QT_VERSION >= 0x040200
#include <QHBoxLayout>
#include <QPushButton>
#endif // QT_VERSION
#include <QApplication>
#include <QVBoxLayout>


void QxtConfigDialogPrivate::init( QxtConfigWidget::IconPosition pos )
{
    QxtConfigDialog* p = &qxt_p();
	configWidget = new QxtConfigWidget(pos);
#if QT_VERSION >= 0x040200
    buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, p);
    QObject::connect(buttons, SIGNAL(accepted()), p, SLOT(accept()));
    QObject::connect(buttons, SIGNAL(rejected()), p, SLOT(reject()));
#else // QT_VERSION >= 0x040200
    buttons = new QWidget(p);
    QHBoxLayout* layout = new QHBoxLayout(buttons);
    QPushButton* okButton = new QPushButton(QxtConfigDialog::tr("&OK"));
    QPushButton* cancelButton = new QPushButton(QxtConfigDialog::tr("&Cancel"));
    QObject::connect(okButton, SIGNAL(clicked()), p, SLOT(accept()));
    QObject::connect(cancelButton, SIGNAL(clicked()), p, SLOT(reject()));
    layout->addStretch();
    layout->addWidget(okButton);
    layout->addWidget(cancelButton);
#endif
    layout = new QVBoxLayout(p);
    layout->addWidget(configWidget);
    layout->addWidget(buttons);
}

/*!
    \class QxtConfigDialog QxtConfigDialog
    \inmodule QxtGui
    \brief A configuration dialog.

    QxtConfigDialog provides a convenient interface for building
    common configuration dialogs. QxtConfigDialog consists of a
    list of icons and a stack of pages.

    Example usage:
    \code
    QxtConfigDialog dialog;
    dialog.addPage(new ConfigurationPage(&dialog), QIcon(":/images/config.png"), tr("Configuration"));
    dialog.addPage(new UpdatePage(&dialog), QIcon(":/images/update.png"), tr("Update"));
    dialog.addPage(new QueryPage(&dialog), QIcon(":/images/query.png"), tr("Query"));
    dialog.exec();
    \endcode

    \image qxtconfigdialog.png "QxtConfigDialog with page icons on the left (QxtConfigDialog::West)."
 */

/*!
    \enum IconPosition::IconPosition

    This enum describes the page icon position.

    \sa QxtCheckComboBox::iconPosition
 */

/*!
    \value QxtConfigDialog::IconPosition QxtConfigDialog::North

    The icons are located above the pages.
 */

/*!
    \value QxtConfigDialog::IconPosition QxtConfigDialog::West

    The icons are located to the left of the pages.
 */

/*!
    \value QxtConfigDialog::IconPosition QxtConfigDialog::East

    The icons are located to the right of the pages.
 */

/*!
    \fn QxtConfigDialog::currentIndexChanged(int index)

    This signal is emitted whenever the current page \a index changes.

    \sa currentIndex()
 */

/*!
    Constructs a new QxtConfigDialog with \a parent and \a flags.
 */
QxtConfigDialog::QxtConfigDialog(QWidget* parent, Qt::WindowFlags flags)
        : QDialog(parent, flags)
{
    QXT_INIT_PRIVATE(QxtConfigDialog);
    qxt_d().init(QxtConfigWidget::West);
}

/*!
    Constructs a new QxtConfigDialog with icon \a position, \a parent and \a flags.
 */
QxtConfigDialog::QxtConfigDialog(QxtConfigWidget::IconPosition position, QWidget* parent, Qt::WindowFlags flags)
        : QDialog(parent, flags)
{
    QXT_INIT_PRIVATE(QxtConfigDialog);
    qxt_d().init(position);
}

/*!
    Destructs the config dialog.
 */
QxtConfigDialog::~QxtConfigDialog()
{}

/*!
    Returns the dialog button box.

    The default buttons are \bold QDialogButtonBox::Ok and \bold QDialogButtonBox::Cancel.

    \bold {Note:} QDialogButtonBox is available in Qt 4.2 or newer.

    \sa setDialogButtonBox()
*/
#if QT_VERSION >= 0x040200
QDialogButtonBox* QxtConfigDialog::dialogButtonBox() const
{
    return qxt_d().buttons;
}
#endif // QT_VERSION

/*!
    Sets the dialog \a buttonBox.

    \sa dialogButtonBox()
*/
#if QT_VERSION >= 0x040200
void QxtConfigDialog::setDialogButtonBox(QDialogButtonBox* buttonBox)
{
    if (qxt_d().buttons != buttonBox)
    {
        if (qxt_d().buttons && qxt_d().buttons->parent() == this)
        {
            delete qxt_d().buttons;
			qxt_d().buttons = NULL;
        }
        qxt_d().buttons = buttonBox;
		qxt_d().layout->addWidget(qxt_d().buttons);
    }
}
#endif // QT_VERSION

/*!
    \property QxtConfigDialog::hoverEffect
    \brief This property holds whether a hover effect is shown for page icons

    The default value is \bold true.

    \bold {Note:} Hovered (but not selected) icons are highlighted with lightened \bold QPalette::Highlight
    (whereas selected icons are highlighted with \bold QPalette::Highlight). In case lightened
    \bold QPalette::Highlight ends up same as \bold QPalette::Base, \bold QPalette::AlternateBase is used
    as a fallback color for the hover effect. This usually happens when \bold QPalette::Highlight
    already is a light color (eg. light gray).
 */
bool QxtConfigDialog::hasHoverEffect() const
{
    return qxt_d().configWidget->hasHoverEffect();
}

void QxtConfigDialog::setHoverEffect(bool enabled)
{
    qxt_d().configWidget->setHoverEffect(enabled);
}

/*!
    \property QxtConfigDialog::iconPosition
    \brief This property holds the position of page icons
 */
QxtConfigWidget::IconPosition QxtConfigDialog::iconPosition() const
{
    return qxt_d().configWidget->iconPosition();
}

void QxtConfigDialog::setIconPosition(QxtConfigWidget::IconPosition position)
{
	qxt_d().configWidget->setIconPosition(position);
}

/*!
    \property QxtConfigDialog::iconSize
    \brief This property holds the size of page icons
 */
QSize QxtConfigDialog::iconSize() const
{
    return qxt_d().configWidget->iconSize();
}

void QxtConfigDialog::setIconSize(const QSize& size)
{
    qxt_d().configWidget->setIconSize(size);
}

/*!
    Adds a \a page with \a icon and \a title.

    In case \a title is an empty string, \bold QWidget::windowTitle is used.

    Returns the index of added page.

    \warning Adding and removing pages dynamically at run time might cause flicker.

    \sa insertPage()
*/
int QxtConfigDialog::addPage(QWidget* page, const QIcon& icon, const QString& title)
{
    return qxt_d().configWidget->insertPage(-1, page, icon, title);
}

/*!
    Inserts a \a page with \a icon and \a title.

    In case \a title is an empty string, \bold QWidget::windowTitle is used.

    Returns the index of inserted page.

    \warning Inserting and removing pages dynamically at run time might cause flicker.

    \sa addPage()
*/
int QxtConfigDialog::insertPage(int index, QWidget* page, const QIcon& icon, const QString& title)
{
    return qxt_d().configWidget->insertPage(index, page, icon, title);
}

/*!
   Removes the page at \a index and returns it.

   \bold {Note:} Does not delete the page widget.
*/
QWidget* QxtConfigDialog::takePage(int index)
{
	return qxt_d().configWidget->takePage(index);
}

/*!
    \property QxtConfigDialog::count
    \brief This property holds the number of pages
*/
int QxtConfigDialog::count() const
{
    return qxt_d().configWidget->count();
}

/*!
    \property QxtConfigDialog::currentIndex
    \brief This property holds the index of current page
*/
int QxtConfigDialog::currentIndex() const
{
    return qxt_d().configWidget->currentIndex();
}

void QxtConfigDialog::setCurrentIndex(int index)
{
    qxt_d().configWidget->setCurrentIndex(index);
}

/*!
    Returns the current page.

    \sa currentIndex(), setCurrentPage()
*/
QWidget* QxtConfigDialog::currentPage() const
{
    return qxt_d().configWidget->currentPage();
}

/*!
    Sets the current \a page.

    \sa currentPage(), currentIndex()
*/
void QxtConfigDialog::setCurrentPage(QWidget* page)
{
    qxt_d().configWidget->setCurrentPage(page);
}

/*!
    Returns the index of \a page or \bold -1 if the page is unknown.
*/
int QxtConfigDialog::indexOf(QWidget* page) const
{
    return qxt_d().configWidget->indexOf(page);
}

/*!
    Returns the page at \a index or \bold 0 if the \a index is out of range.
*/
QWidget* QxtConfigDialog::page(int index) const
{
    return qxt_d().configWidget->page(index);
}

/*!
    Returns \bold true if the page at \a index is enabled; otherwise \bold false.

    \sa setPageEnabled(), QWidget::isEnabled()
*/
bool QxtConfigDialog::isPageEnabled(int index) const
{
    return qxt_d().configWidget->isPageEnabled(index);
}

/*!
    Sets the page at \a index \a enabled. The corresponding
    page icon is also \a enabled.

    \sa isPageEnabled(), QWidget::setEnabled()
*/
void QxtConfigDialog::setPageEnabled(int index, bool enabled)
{
    qxt_d().configWidget->setPageEnabled(index,enabled);
}

/*!
    Returns \bold true if the page at \a index is hidden; otherwise \bold false.

    \sa setPageHidden(), QWidget::isVisible()
*/
bool QxtConfigDialog::isPageHidden(int index) const
{
    return qxt_d().configWidget->isPageHidden(index);
}

/*!
    Sets the page at \a index \a hidden. The corresponding
    page icon is also \a hidden.

    \sa isPageHidden(), QWidget::setVisible()
*/
void QxtConfigDialog::setPageHidden(int index, bool hidden)
{
	qxt_d().configWidget->setPageHidden(index,hidden);
}

/*!
    Returns the icon of page at \a index.

    \sa setPageIcon()
*/
QIcon QxtConfigDialog::pageIcon(int index) const
{
	return qxt_d().configWidget->pageIcon(index);
}

/*!
    Sets the \a icon of page at \a index.

    \sa pageIcon()
*/
void QxtConfigDialog::setPageIcon(int index, const QIcon& icon)
{
	qxt_d().configWidget->setPageIcon(index,icon);
}

/*!
    Returns the title of page at \a index.

    \sa setPageTitle()
*/
QString QxtConfigDialog::pageTitle(int index) const
{
	return qxt_d().configWidget->pageTitle(index);
}

/*!
    Sets the \a title of page at \a index.

    \sa pageTitle()
*/
void QxtConfigDialog::setPageTitle(int index, const QString& title)
{
	qxt_d().configWidget->setPageTitle(index,title);
}

/*!
    Returns the tooltip of page at \a index.

    \sa setPageToolTip()
*/
QString QxtConfigDialog::pageToolTip(int index) const
{
	return qxt_d().configWidget->pageToolTip(index);
}

/*!
    Sets the \a tooltip of page at \a index.

    \sa pageToolTip()
*/
void QxtConfigDialog::setPageToolTip(int index, const QString& tooltip)
{
	qxt_d().configWidget->setPageToolTip(index,tooltip);
}

/*!
    Returns the what's this of page at \a index.

    \sa setPageWhatsThis()
*/
QString QxtConfigDialog::pageWhatsThis(int index) const
{
	return qxt_d().configWidget->pageWhatsThis(index);
}

/*!
    Sets the \a whatsthis of page at \a index.

    \sa pageWhatsThis()
*/
void QxtConfigDialog::setPageWhatsThis(int index, const QString& whatsthis)
{
	qxt_d().configWidget->setPageWhatsThis(index,whatsthis);
}

/*!
    Reimplemented from QDialog.

    \bold {Note:} The default implementation calls SLOT(accept()) of
    each page page provided that such slot exists.

    \sa reject()
 */
void QxtConfigDialog::accept()
{
	qxt_d().configWidget->accept();
    QDialog::accept();
}

/*!
    Reimplemented from QDialog.

    \bold {Note:} The default implementation calls SLOT(reject()) of
    each page provided that such slot exists.

    \sa accept()
 */
void QxtConfigDialog::reject()
{
	qxt_d().configWidget->reject();
    QDialog::reject();
}