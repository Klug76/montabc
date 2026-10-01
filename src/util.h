#pragma once

#include "montabc.h"

#ifdef MONTABC_TINY
/* Компилятор генерирует вызовы memset/memcpy даже без CRT — даём свои. */
void *memset(void *dst, int val, size_t count);
void *memcpy(void *dst, const void *src, size_t count);
#endif

int Ui_Scale(int logicalPx, UINT dpi);
int Util_ClampI(int v, int lo, int hi);
double Util_ClampD(double v, double lo, double hi);

/* Парсер/форматтер процентов вида "10.5" (свой формат из settings.ini). */
double Util_WtoD(const WCHAR *s);
void Util_DtoW(WCHAR *buf, double v);
