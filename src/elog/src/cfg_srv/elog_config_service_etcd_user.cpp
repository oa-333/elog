#include "cfg_srv/elog_config_service_etcd_user.h"

#ifdef ELOG_ENABLE_CONFIG_PUBLISH_ETCD

#include "elog_common.h"
#include "elog_field_selector_internal.h"
#include "elog_report.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogConfigServiceEtcdUser)

bool ELogConfigServiceEtcdUser::initializeEtcd() {
    // verify user provided all details
    if (m_params.m_serverList.empty()) {
        ELOG_REPORT_ERROR("Cannot start etcd configuration service user: no etcd server defined");
        return false;
    }
    if (m_params.m_dirPrefix.empty()) {
        ELOG_REPORT_ERROR(
            "Cannot start etcd configuration service user: no publish key prefix defined");
        return false;
    }
    if (m_params.m_key.empty()) {
        ELOG_REPORT_ERROR("Cannot start etcd configuration service user: no publish key defined");
        return false;
    }

    // format full key path, remove initial slash if any
    m_dir = m_params.m_dirPrefix;
    if (m_params.m_dirPrefix.front() == '/') {
        m_dir = m_params.m_dirPrefix.substr(1);
    } else {
        m_dir = m_params.m_dirPrefix;
    }
    if (m_dir.back() != '/') {
        m_dir += "/";
    }

    // prepare credentials
    if (!m_params.m_user.empty()) {
        std::string credentials = m_params.m_user + ":" + m_params.m_password;
        m_encodedCredentials = std::string("Basic ") + encodeBase64(credentials);
    }

    // connect will take place on first request

    return true;
}

bool ELogConfigServiceEtcdUser::terminateEtcd() {
    if (isEtcdConnected()) {
        if (!m_etcdClient.stop()) {
            ELOG_REPORT_ERROR("Failed to stop HTTP client");
            return false;
        }
    }
    return true;
}

bool ELogConfigServiceEtcdUser::isEtcdConnected() { return m_connected; }

bool ELogConfigServiceEtcdUser::connectEtcd() {
    for (uint32_t i = 0; i < m_params.m_serverList.size(); ++i) {
        std::string serverName = std::string("etcd-server-") + std::to_string(m_currServer);
        m_etcdClient.initialize(m_params.m_serverList[m_currServer].getDetails(),
                                serverName.c_str(), m_httpConfig, this, true);

        if (m_etcdClient.start()) {
            // we assume connected until encountering error
            m_connected = true;
            return true;
        } else {
            ELOG_REPORT_NOTICE("Failed to connect to etcd server at %s",
                               m_params.m_serverList[m_currServer].getDetails());
            m_currServer = (m_currServer + 1) % m_params.m_serverList.size();
        }
    }

    ELOG_REPORT_ERROR("Failed to connect to etcd server(s)");
    return false;
}

std::string ELogConfigServiceEtcdUser::encodeBase64(const std::string& inputStr) {
    // Character set of base64 encoding scheme
    char char_set[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    // Resultant string
    std::string resStr;

    size_t index, no_of_bits = 0, padding = 0, val = 0, count = 0, temp;
    size_t i, j = 0;

    // Loop takes 3 characters at a time from
    // input_str and stores it in val
    for (i = 0; i < inputStr.length(); i += 3) {
        val = 0, count = 0, no_of_bits = 0;

        for (j = i; j < inputStr.length() && j <= i + 2; j++) {
            // binary data of input_str is stored in val
            val = val << 8;

            // (A + 0 = A) stores character in val
            val = val | inputStr[j];

            // calculates how many time loop
            // ran if "MEN" -> 3 otherwise "ON" -> 2
            count++;
        }

        no_of_bits = count * 8;

        // calculates how many "=" to append after resStr.
        padding = no_of_bits % 3;

        // extracts all bits from val (6 at a time)
        // and find the value of each block
        while (no_of_bits != 0) {
            // retrieve the value of each block
            if (no_of_bits >= 6) {
                temp = no_of_bits - 6;

                // binary of 63 is (111111) f
                index = (val >> temp) & 63;
                no_of_bits -= 6;
            } else {
                temp = 6 - no_of_bits;

                // append zeros to right if bits are less than 6
                index = (val << temp) & 63;
                no_of_bits = 0;
            }
            resStr += char_set[index];
        }
    }

    // padding is done here
    for (i = 1; i <= padding; i++) {
        resStr += '=';
    }

    return resStr;
}

std::string ELogConfigServiceEtcdUser::decodeBase64(const std::string& encoded) {
    std::string decodedStr;

    size_t i, j = 0;

    // stores the bitstream.
    int num = 0;

    // count_bits stores current
    // number of bits in num.
    int count_bits = 0;

    // selects 4 characters from
    // encoded string at a time.
    // find the position of each encoded
    // character in char_set and stores in num.
    for (i = 0; i < encoded.length(); i += 4) {
        num = 0, count_bits = 0;
        for (j = 0; j < 4; j++) {
            // make space for 6 bits.
            if (encoded[i + j] != '=') {
                num = num << 6;
                count_bits += 6;
            }

            /* Finding the position of each encoded
            character in char_set
            and storing in "num", use OR
            '|' operator to store bits.*/

            // encoded[i + j] = 'E', 'E' - 'A' = 5
            // 'E' has 5th position in char_set.
            if (encoded[i + j] >= 'A' && encoded[i + j] <= 'Z') num = num | (encoded[i + j] - 'A');

            // encoded[i + j] = 'e', 'e' - 'a' = 5,
            // 5 + 26 = 31, 'e' has 31st position in char_set.
            else if (encoded[i + j] >= 'a' && encoded[i + j] <= 'z')
                num = num | (encoded[i + j] - 'a' + 26);

            // encoded[i + j] = '8', '8' - '0' = 8
            // 8 + 52 = 60, '8' has 60th position in char_set.
            else if (encoded[i + j] >= '0' && encoded[i + j] <= '9')
                num = num | (encoded[i + j] - '0' + 52);

            // '+' occurs in 62nd position in char_set.
            else if (encoded[i + j] == '+')
                num = num | 62;

            // '/' occurs in 63rd position in char_set.
            else if (encoded[i + j] == '/')
                num = num | 63;

            // ( str[i + j] == '=' ) remove 2 bits
            // to delete appended bits during encoding.
            else {
                num = num >> 2;
                count_bits -= 2;
            }
        }

        while (count_bits != 0) {
            count_bits -= 8;

            // 255 in binary is 11111111
            decodedStr += (char)((num >> count_bits) & 255);
        }
    }

    return decodedStr;
}

}  // namespace elog

#endif  // ELOG_ENABLE_CONFIG_PUBLISH_ETCD