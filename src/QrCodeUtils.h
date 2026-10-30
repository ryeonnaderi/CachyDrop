
#pragma once

#include <QByteArray>
#include <QStringList>

namespace QrCodeUtils
{

// Converts text, such as a pairing URL, into QR-code rows.
// Each row contains '1' for a dark module and '0' for a light module.
// Returns an empty list if QR generation fails.
QStringList generateRows(const QByteArray &text);

} // namespace QrCodeUtils
