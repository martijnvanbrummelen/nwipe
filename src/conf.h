#ifndef CONF_H_
#define CONF_H_

/**
 * Initialises the libconfig code, called once at the
 * start of nwipe, prior to any attempts to access
 * nwipe's config file /etc/nwipe/nwipe.conf
 * @param none
 * @return int
 *   0  = success
 *   -1 = error
 */
int nwipe_conf_init();

/**
 * Before exiting nwipe, this function should be called
 * to free up libconfig's memory usage
 * @param none
 * @return void
 */
void nwipe_conf_close();

void save_selected_customer( char** );

#define FIELD_LENGTH 256
#define NUMBER_OF_FIELDS 4

#endif /* CONF_H_ */
