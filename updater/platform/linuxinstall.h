// SPDX-License-Identifier: MIT
#pragma once
#include "linuxmanifest.h"

namespace tss::appimage {
// Возвращает путь сохранённой прежней версии. Ошибка до commit не меняет original.
QString install(const QString& original, const QString& candidate, const Release& release);
}
