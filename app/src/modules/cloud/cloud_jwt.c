/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/* Application-side nRF Cloud JWT generation for targets whose modem cannot sign
 * JWTs and whose private key cannot be read back from credential storage.
 *
 * nRF9251 has neither the modem %JWT path used by CONFIG_MODEM_JWT nor a
 * readable private key: modem_key_mgmt refuses to export it, so the upstream
 * CONFIG_NRF_CLOUD_JWT_SOURCE_CUSTOM path fails with -EACCES in
 * tls_credential_get().
 *
 * Rather than patching nrf_cloud_jwt.c, wrap the public entry point.
 * nrf_cloud_jwt_generate() is declared in net/nrf_cloud.h and every caller
 * (nrf_cloud_coap_transport.c, nrf_cloud_coap_download.c, location_utils.c)
 * lives in a different translation unit, so --wrap intercepts all of them.
 *
 * This is a bring-up mechanism: the signing key is compiled into the image.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <app_jwt.h>
#include <net/nrf_cloud.h>
#include <psa/crypto.h>

LOG_MODULE_REGISTER(cloud_jwt, CONFIG_APP_CLOUD_LOG_LEVEL);

/* Size of the ES256 private key material */
#define PRV_KEY_SZ 32

static int builtin_key_import(psa_key_id_t *const key_id)
{
	static const char key_hex[] = CONFIG_APP_CLOUD_JWT_TEST_KEY_HEX;
	psa_key_attributes_t key_attributes = PSA_KEY_ATTRIBUTES_INIT;
	uint8_t priv_key[PRV_KEY_SZ];
	psa_status_t status;
	size_t len;

	if (strlen(key_hex) != (PRV_KEY_SZ * 2)) {
		LOG_ERR("CONFIG_APP_CLOUD_JWT_TEST_KEY_HEX must be %d characters, is %u",
			PRV_KEY_SZ * 2, (unsigned int)strlen(key_hex));

		return -EINVAL;
	}

	len = hex2bin(key_hex, strlen(key_hex), priv_key, sizeof(priv_key));
	if (len != PRV_KEY_SZ) {
		LOG_ERR("Failed to decode the built-in key, decoded %u bytes", (unsigned int)len);

		return -EBADF;
	}

	psa_set_key_usage_flags(&key_attributes,
				PSA_KEY_USAGE_SIGN_MESSAGE | PSA_KEY_USAGE_VERIFY_MESSAGE);
	psa_set_key_lifetime(&key_attributes, PSA_KEY_LIFETIME_VOLATILE);
	psa_set_key_algorithm(&key_attributes, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
	psa_set_key_type(&key_attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
	psa_set_key_bits(&key_attributes, 256);

	status = psa_import_key(&key_attributes, priv_key, sizeof(priv_key), key_id);

	/* The key material is in PSA now; do not leave a copy on the stack. */
	memset(priv_key, 0, sizeof(priv_key));

	if (status != PSA_SUCCESS) {
		LOG_ERR("psa_import_key failed, status: %d", (int)status);

		return -EIO;
	}

	return 0;
}

int __wrap_nrf_cloud_jwt_generate(uint32_t time_valid_s, char *const jwt_buf, size_t jwt_buf_sz)
{
	char client_id[NRF_CLOUD_CLIENT_ID_MAX_LEN + 1];
	char iss[NRF_CLOUD_CLIENT_ID_MAX_LEN + sizeof(CONFIG_APP_CLOUD_JWT_ISS_HW_PREFIX) + 2];
	const char *issuer = NULL;
	uint32_t validity_s = time_valid_s;
	psa_key_id_t key_id;
	psa_status_t status;
	int err;

	if (!jwt_buf || !jwt_buf_sz) {
		return -EINVAL;
	}

	if (time_valid_s == 0) {
		validity_s = NRF_CLOUD_JWT_VALID_TIME_S_DEF;
	} else if (time_valid_s > NRF_CLOUD_JWT_VALID_TIME_S_MAX) {
		validity_s = NRF_CLOUD_JWT_VALID_TIME_S_MAX;
	}

	err = nrf_cloud_client_id_get(client_id, sizeof(client_id));
	if (err) {
		LOG_ERR("nrf_cloud_client_id_get, error: %d", err);

		return err;
	}

	/* nRF Cloud verifies the iss claim as <hardware>.<device ID>, matching the
	 * format the modem uses for %JWT. Omit iss when no prefix is configured.
	 */
	if (sizeof(CONFIG_APP_CLOUD_JWT_ISS_HW_PREFIX) > 1) {
		int len = snprintf(iss, sizeof(iss), "%s.%s",
				   CONFIG_APP_CLOUD_JWT_ISS_HW_PREFIX, client_id);

		if ((len < 0) || (len >= (int)sizeof(iss))) {
			LOG_ERR("iss claim does not fit in %u bytes", (unsigned int)sizeof(iss));

			return -ENOMEM;
		}

		issuer = iss;
	}

	status = psa_crypto_init();
	if (status != PSA_SUCCESS) {
		LOG_ERR("psa_crypto_init failed, status: %d", (int)status);

		return -EIO;
	}

	err = builtin_key_import(&key_id);
	if (err) {
		return err;
	}

	struct app_jwt_data jwt = {
		.sec_tag = key_id,
		.key_type = JWT_KEY_TYPE_CLIENT_PRIV,
		.alg = JWT_ALG_TYPE_ES256,
		.validity_s = validity_s,
		.jwt_buf = jwt_buf,
		.jwt_sz = jwt_buf_sz,
		.subject = client_id,
		.issuer = issuer,
	};

	err = app_jwt_generate(&jwt);
	if (err) {
		LOG_ERR("app_jwt_generate, error: %d", err);
	}

	(void)psa_destroy_key(key_id);

	return err;
}
