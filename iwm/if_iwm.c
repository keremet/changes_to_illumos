#include <sys/modctl.h>
#include <sys/ddi.h>
#include <sys/conf.h>
#include <sys/sunddi.h>
#include <sys/mac_provider.h>
#include <sys/mac_wifi.h>
#include <sys/net80211.h>
#include <sys/stat.h>

#define IWM_SUCCESS	0


static void *iwm_state = NULL;

struct iwm_softc
{
	struct ieee80211com	sc_ic;
	dev_info_t		*sc_dip;
	ddi_acc_handle_t	sc_pcih;
};

/*
 * Mac Call Back entries
 */
static int	iwm_m_stat(void *, uint_t, uint64_t *);
static int	iwm_m_start(void *);
static void	iwm_m_stop(void *);
static int	iwm_m_unicst(void *, const uint8_t *);
static int	iwm_m_multicst(void *, boolean_t, const uint8_t *);
static int	iwm_m_promisc(void *, boolean_t);
static mblk_t	*iwm_m_tx(void *, mblk_t *);
static void	iwm_m_ioctl(void *, queue_t *, mblk_t *);
static int	iwm_m_setprop(void *, const char *, mac_prop_id_t, uint_t,
    const void *);
static int	iwm_m_getprop(void *, const char *, mac_prop_id_t, uint_t,
    void *);
static void	iwm_m_propinfo(void *, const char *, mac_prop_id_t,
    mac_prop_info_handle_t);

mac_callbacks_t	iwm_m_callbacks = {
	.mc_callbacks	= MC_IOCTL | MC_SETPROP | MC_GETPROP | MC_PROPINFO,
	.mc_getstat	= iwm_m_stat,
	.mc_start	= iwm_m_start,
	.mc_stop	= iwm_m_stop,
	.mc_setpromisc	= iwm_m_promisc,
	.mc_multicst	= iwm_m_multicst,
	.mc_unicst	= iwm_m_unicst,
	.mc_tx		= iwm_m_tx,
	.mc_reserved	= NULL,
	.mc_ioctl	= iwm_m_ioctl,
	.mc_getcapab	= NULL,
	.mc_open	= NULL,
	.mc_close	= NULL,
	.mc_setprop	= iwm_m_setprop,
	.mc_getprop	= iwm_m_getprop,
	.mc_propinfo	= iwm_m_propinfo
};

/*
 * invoked by GLD get statistics from NIC and driver
 */
static int
iwm_m_stat(void *arg, uint_t stat, uint64_t *val)
{
	return (ENOTSUP);
}

/*
 * invoked by GLD to start or open NIC
 */
static int
iwm_m_start(void *arg)
{
	return (IWM_SUCCESS);
}

/*
 * invoked by GLD to stop or down NIC
 */
static void
iwm_m_stop(void *arg)
{

}

/*
 * invoked by GLD to configure NIC
 */
static int
iwm_m_unicst(void *arg, const uint8_t *macaddr)
{
	return (IWM_SUCCESS);
}

/*ARGSUSED*/
static int
iwm_m_multicst(void *arg, boolean_t add, const uint8_t *m)
{
	return (IWM_SUCCESS);
}

/*ARGSUSED*/
static int
iwm_m_promisc(void *arg, boolean_t on)
{
	_NOTE(ARGUNUSED(on));

	return (IWM_SUCCESS);
}

static mblk_t *
iwm_m_tx(void *arg, mblk_t *mp)
{
	return (NULL);
}

static void
iwm_m_ioctl(void *arg, queue_t *wq, mblk_t *mp)
{

}


/*
 * Call back functions for get/set property
 */
static int
iwm_m_getprop(void *arg, const char *pr_name, mac_prop_id_t wldp_pr_num,
    uint_t wldp_length, void *wldp_buf)
{
	return 0;
}

static void
iwm_m_propinfo(void *arg, const char *pr_name, mac_prop_id_t wldp_pr_num,
    mac_prop_info_handle_t prh)
{

}

static int
iwm_m_setprop(void *arg, const char *pr_name, mac_prop_id_t wldp_pr_num,
    uint_t wldp_length, const void *wldp_buf)
{
	return (ENETRESET);
}

static int
iwm_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	cmn_err(CE_WARN, "iwm_attach entered");
	int instance;

	struct iwm_softc *sc;
	struct ieee80211com *ic;
	char strbuf[32];
	wifi_data_t wd = { 0 };
	mac_register_t *macp;
	int error;

	switch (cmd) {
	case DDI_ATTACH:
		break;
	default:
		return (DDI_FAILURE);
	}

	instance = ddi_get_instance(dip);

	if (ddi_soft_state_zalloc(iwm_state, instance) != DDI_SUCCESS) {
		dev_err(dip, CE_WARN, "!ddi_soft_state_zalloc() failed");
		return (DDI_FAILURE);
	}

	sc = ddi_get_soft_state(iwm_state, instance);
	ddi_set_driver_private(dip, (caddr_t)sc);
	
	ic = &sc->sc_ic;

	sc->sc_dip = dip;

	/*
	 * Initialize pointer to device specific functions
	 */
	wd.wd_secalloc = WIFI_SEC_NONE;
	wd.wd_opmode = ic->ic_opmode;
	IEEE80211_ADDR_COPY(wd.wd_bssid, ic->ic_macaddr);

	/*
	 * create relation to GLD
	 */
	macp = mac_alloc(MAC_VERSION);
	if (NULL == macp) {
		dev_err(sc->sc_dip, CE_WARN, "!mac_alloc() failed");
		goto fail_mac_alloc;
	}

	macp->m_type_ident	= MAC_PLUGIN_IDENT_WIFI;
	macp->m_driver		= sc;
	macp->m_dip		= dip;
	macp->m_src_addr	= ic->ic_macaddr;
	macp->m_callbacks	= &iwm_m_callbacks;
	macp->m_min_sdu		= 0;
	macp->m_max_sdu		= IEEE80211_MTU;
	macp->m_pdata		= &wd;
	macp->m_pdata_size	= sizeof (wd);

	/*
	 * Register the macp to mac
	 */
	error = mac_register(macp, &ic->ic_mach);
	mac_free(macp);
	if (error != DDI_SUCCESS) {
		dev_err(sc->sc_dip, CE_WARN, "!mac_register() failed");
		goto fail_mac_alloc;
	}

	/*
	 * Create minor node of type DDI_NT_NET_WIFI
	 */
	(void) snprintf(strbuf, sizeof (strbuf), "iwm%d", instance);
	error = ddi_create_minor_node(dip, strbuf, S_IFCHR,
	    instance + 1, DDI_NT_NET_WIFI, 0);
	if (error != DDI_SUCCESS) {
		dev_err(sc->sc_dip, CE_WARN, "!ddi_create_minor_node() failed");
		goto fail_minor;
	}

	/*
	 * Notify link is down now
	 */
	mac_link_update(ic->ic_mach, LINK_STATE_DOWN);




	return (DDI_SUCCESS);

fail_minor:
	mac_unregister(ic->ic_mach);

fail_mac_alloc:

fail_pci_config:
	ddi_soft_state_free(iwm_state, instance);

	return (DDI_FAILURE);
}

static int
iwm_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	cmn_err(CE_NOTE, "iwm_detach entered");
	switch (cmd) {
	case DDI_DETACH:
		break;
	default:
		return (DDI_FAILURE);
	}
	return (DDI_SUCCESS);
}
/*
 * Module Loading Data & Entry Points
 */
DDI_DEFINE_STREAM_OPS(iwm_devops, nulldev, nulldev, iwm_attach,
    iwm_detach, nodev, NULL, D_MP, NULL, ddi_quiesce_not_supported); /*do iwm_quiesce*/

static struct modldrv iwm_modldrv = {
	&mod_driverops,
	"Intel WiFi 7565 driver",
	&iwm_devops
};

static struct modlinkage iwm_modlinkage = {
	MODREV_1,
	&iwm_modldrv,
	NULL
};

int
_init(void)
{
	cmn_err(CE_WARN, "iwm _init entered");

	int	status;

	status = ddi_soft_state_init(&iwm_state,
	    sizeof (struct iwm_softc), 1);
	if (status != DDI_SUCCESS)
		return (status);

	mac_init_ops(&iwm_devops, "iwm");
	status = mod_install(&iwm_modlinkage);
	if (status != DDI_SUCCESS) {
		mac_fini_ops(&iwm_devops);
		ddi_soft_state_fini(&iwm_state);
	}

	return (status);
}

int
_fini(void)
{
	int status;

	status = mod_remove(&iwm_modlinkage);
	if (status == DDI_SUCCESS) {
		mac_fini_ops(&iwm_devops);
		ddi_soft_state_fini(&iwm_state);
	}

	return (status);
}

int
_info(struct modinfo *mip)
{
	return (mod_info(&iwm_modlinkage, mip));
}
