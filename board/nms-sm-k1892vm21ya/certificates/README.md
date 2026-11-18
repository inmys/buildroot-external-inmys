# TLS CA Минцифры

PEM-файлы скачаны 18.11.2026 с официального CDN Госуслуг:

- https://gu-st.ru/content/lending/russian_trusted_root_ca_pem.crt
- https://gu-st.ru/content/lending/russian_trusted_sub_ca_pem.crt

Страница установки: https://www.gosuslugi.ru/crt.
Переводы строк нормализованы в LF, добавлен завершающий LF для CA bundle;
содержимое DER сертификатов не изменено. Файлы хранятся в external tree;
сборка не скачивает их заново.
`external.mk` устанавливает их до штатной генерации CA bundle Buildroot.
Хранилище общее для приложений, использующих системные CA, включая Cog.

| Сертификат | Действует до (UTC) | SHA-256 fingerprint DER |
| --- | --- | --- |
| Russian Trusted Root CA | 27.02.2032 21:04:15 | `D26D2D0231B7C39F92CC738512BA54103519E4405D68B5BD703E9788CA8ECF31` |
| Russian Trusted Sub CA | 06.03.2027 11:25:19 | `BBBDE2103E790B999EC62BD03CF625A5A2E7C316E10AFE6A490EEDEAD8B3FD9B` |

При обновлении проверить источник, CA extensions, подпись цепочки и сроки;
заменить файлы и выполнить `make` в output для пересборки образов.
