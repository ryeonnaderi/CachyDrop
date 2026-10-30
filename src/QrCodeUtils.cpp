
#include "QrCodeUtils.h"

#include <qrencode.h>

namespace QrCodeUtils
{

QStringList generateRows(const QByteArray &text)
{
    if (text.isEmpty())
        return {};

    QRcode *qr = QRcode_encodeString(
        text.constData(),
        0,
        QR_ECLEVEL_M,
        QR_MODE_8,
        1
    );

    if (qr == nullptr)
        return {};

    QStringList rows;
    rows.reserve(qr->width);

    for (int y = 0; y < qr->width; ++y) {
        QString row;
        row.reserve(qr->width);

        for (int x = 0; x < qr->width; ++x) {
            const unsigned char module =
                qr->data[y * qr->width + x];

            row.append((module & 1)
                           ? QLatin1Char('1')
                           : QLatin1Char('0'));
        }

        rows.append(row);
    }

    QRcode_free(qr);
    return rows;
}

} // namespace QrCodeUtils
