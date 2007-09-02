/****************************************************************************
**
** Copyright (C) Qxt Foundation. Some rights reserved.
**
** This file is part of the QxtCore module of the Qt eXTension library
**
** This library is free software; you can redistribute it and/or modify it
** under the terms of th Common Public License, version 1.0, as published by
** IBM.
**
** This file is provided "AS IS", without WARRANTIES OR CONDITIONS OF ANY
** KIND, EITHER EXPRESS OR IMPLIED INCLUDING, WITHOUT LIMITATION, ANY
** WARRANTIES OR CONDITIONS OF TITLE, NON-INFRINGEMENT, MERCHANTABILITY OR
** FITNESS FOR A PARTICULAR PURPOSE.
**
** You should have received a copy of the CPL along with this file.
** See the LICENSE file and the cpl1.0.txt file included with the source
** distribution for more information. If you did not receive a copy of the
** license, contact the Qxt Foundation.
**
** <http://libqxt.sourceforge.net>  <foundation@libqxt.org>
**
****************************************************************************/

#ifndef QXTMETATYPE_H
#define QXTMETATYPE_H

#include <QMetaType>
#include <QDataStream>
#include <QGenericArgument>
#include <QtDebug>

template <typename T>
class QxtMetaType {
    static inline T* construct(const T* copy = 0) {
        return QMetaType::construct(qMetaTypeId<T>(), reinterpret_cast<const void*>(copy));
    }

    static inline void destroy(T* data) {
        QMetaType::destroy(qMetaTypeId<T>(), data);
    }

    // no need to reimplement isRegistered since this class will fail at compile time if it isn't
    
    static inline bool load(QDataStream& stream, T* data) {
        return QMetaType::load(stream, qMetaTypeId<T>(), reinterpret_cast<void*>(data));
    }

    static inline bool save(QDataStream& stream, const T* data) {
        return QMetaType::save(stream, qMetaTypeId<T>(), reinterpret_cast<const void*>(data));
    }

    static inline int type() {
        return qMetaTypeId<T>();
    }

    static inline const char* name() {
        return QMetaType::typeName(qMetaTypeId<T>());
    }
};

template <>
class QxtMetaType<void> {
    static inline void* construct(const void* copy = 0) {
        Q_UNUSED(copy);
        return 0;
    }

    static inline void destroy(void* data) {
        Q_UNUSED(data);
    }

    static inline bool load(QDataStream& stream, void* data) {
        Q_UNUSED(stream);
        Q_UNUSED(data);
        return false;
    }

    static inline bool save(QDataStream& stream, const void* data) {
        Q_UNUSED(stream);
        Q_UNUSED(data);
        return false;
    }

    static inline int type() {
        return 0;
    }

    static inline const char* name() {
        return 0;
    }
};

inline void* qxtConstructByName(const char* typeName, const void* copy = 0) {
    return QMetaType::construct(QMetaType::type(typeName), copy);
}

inline void qxtDestroyByName(const char* typeName, void* data) {
    QMetaType::destroy(QMetaType::type(typeName), data);
}

inline void* qxtConstructFromGenericArgument(QGenericArgument arg) {
    return qxtConstructByName(arg.name(), arg.data());
}

inline void qxtDestroyFromGenericArgument(QGenericArgument arg) {
    qxtDestroyByName(arg.name(), arg.data());
}

#endif
