#include <sys/modctl.h>
#include <sys/ddi.h>
#include <sys/conf.h>
#include <sys/sunddi.h>
#include <sys/mac_provider.h>
#include <sys/mac_wifi.h>
#include <sys/net80211.h>
#include <sys/stat.h>

#define IWM_SUCCESS	0

#define PCI_CFG_RETRY_TIMEOUT	0x41
#define IWM_CSR_HW_REV              (0x028)

#define ENTERED cmn_err(CE_WARN, "%s entered", __func__)

#define IWM_SETBITS(sc, reg, mask)					\
	iwm_write(sc, reg, iwm_read(sc, reg) | (mask))


#define IWM_CSR_HW_IF_CONFIG_REG    (0x000) /* hardware interface config */
#define IWM_CSR_HW_IF_CONFIG_REG_BIT_NIC_READY	(0x00400000) /* PCI_OWN_SEM */

#define IWM_CSR_MBOX_SET_REG		(0x088)
#define IWM_CSR_MBOX_SET_REG_OS_ALIVE	0x20

#define IWM_CSR_HW_IF_CONFIG_REG_PREPARE	(0x08000000) /* WAKE_ME */

static void *iwm_state = NULL;

struct iwm_softc
{
	struct ieee80211com	sc_ic;
	dev_info_t		*sc_dip;
	ddi_acc_handle_t	sc_pcih;
	caddr_t			sc_base;
	ddi_acc_handle_t	sc_regh;
	uint8_t 		hw_type;
	size_t sc_intr_size;
	ddi_intr_handle_t *sc_intr_htable;
	uint_t			sc_intr_pri;
	int			sc_intr_cap;
	int			sc_intr_count;
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

static ddi_device_acc_attr_t iwn_reg_accattr = {
	.devacc_attr_version	= DDI_DEVICE_ATTR_V0,
	.devacc_attr_endian_flags = DDI_STRUCTURE_LE_ACC,
	.devacc_attr_dataorder	= DDI_STRICTORDER_ACC,
	.devacc_attr_access	= DDI_DEFAULT_ACC
};

static inline uint32_t
iwm_read(struct iwm_softc *sc, int reg)
{
	/*LINTED: E_PTR_BAD_CAST_ALIGN*/
	return (ddi_get32(sc->sc_regh, (uint32_t *)(sc->sc_base + reg)));
}

static inline void
iwm_write(struct iwm_softc *sc, int reg, uint32_t val)
{
	/*LINTED: E_PTR_BAD_CAST_ALIGN*/
	ddi_put32(sc->sc_regh, (uint32_t *)(sc->sc_base + reg), val);
}

static void
iwm_intr_teardown(struct iwm_softc *sc)
{
	if (sc->sc_intr_htable != NULL) {
		if ((sc->sc_intr_cap & DDI_INTR_FLAG_BLOCK) != 0) {
			(void) ddi_intr_block_disable(sc->sc_intr_htable,
			    sc->sc_intr_count);
		} else {
			(void) ddi_intr_disable(sc->sc_intr_htable[0]);
		}
		(void) ddi_intr_remove_handler(sc->sc_intr_htable[0]);
		(void) ddi_intr_free(sc->sc_intr_htable[0]);
		sc->sc_intr_htable[0] = NULL;

		kmem_free(sc->sc_intr_htable, sc->sc_intr_size);
		sc->sc_intr_size = 0;
		sc->sc_intr_htable = NULL;
	}
}

/*ARGSUSED1*/
static uint_t
iwm_intr(caddr_t arg, caddr_t unused)
{
	ENTERED;
	_NOTE(ARGUNUSED(unused));
	/*LINTED: E_PTR_BAD_CAST_ALIGN*/
	struct iwm_softc *sc = (struct iwm_softc *)arg;
	uint32_t r1, r2, tmp;

	if (sc == NULL)
		return (DDI_INTR_UNCLAIMED);

	return (DDI_INTR_CLAIMED);
}


static int
iwm_intr_add(struct iwm_softc *sc, int intr_type)
{
	ENTERED;
	int ni, na;
	int ret;
	char *func;

	if (ddi_intr_get_nintrs(sc->sc_dip, intr_type, &ni) != DDI_SUCCESS)
		return (DDI_FAILURE);


	if (ddi_intr_get_navail(sc->sc_dip, intr_type, &na) != DDI_SUCCESS)
		return (DDI_FAILURE);

	sc->sc_intr_size = sizeof (ddi_intr_handle_t);
	sc->sc_intr_htable = kmem_zalloc(sc->sc_intr_size, KM_SLEEP);

	ret = ddi_intr_alloc(sc->sc_dip, sc->sc_intr_htable, intr_type, 0, 1,
	    &sc->sc_intr_count, DDI_INTR_ALLOC_STRICT);
	if (ret != DDI_SUCCESS) {
		dev_err(sc->sc_dip, CE_WARN, "!ddi_intr_alloc() failed");
		return (DDI_FAILURE);
	}

	ret = ddi_intr_get_pri(sc->sc_intr_htable[0], &sc->sc_intr_pri);
	if (ret != DDI_SUCCESS) {
		dev_err(sc->sc_dip, CE_WARN, "!ddi_intr_get_pri() failed");
		return (DDI_FAILURE);
	}

	ret = ddi_intr_add_handler(sc->sc_intr_htable[0], iwm_intr, (caddr_t)sc,
	    NULL);
	if (ret != DDI_SUCCESS) {
		dev_err(sc->sc_dip, CE_WARN, "!ddi_intr_add_handler() failed");
		return (DDI_FAILURE);
	}

	ret = ddi_intr_get_cap(sc->sc_intr_htable[0], &sc->sc_intr_cap);
	if (ret != DDI_SUCCESS) {
		dev_err(sc->sc_dip, CE_WARN, "!ddi_intr_get_cap() failed");
		return (DDI_FAILURE);
	}

	if ((sc->sc_intr_cap & DDI_INTR_FLAG_BLOCK) != 0) {
		ret = ddi_intr_block_enable(sc->sc_intr_htable,
		    sc->sc_intr_count);
		func = "ddi_intr_enable_block";
	} else {
		ret = ddi_intr_enable(sc->sc_intr_htable[0]);
		func = "ddi_intr_enable";
	}

	if (ret != DDI_SUCCESS) {
		dev_err(sc->sc_dip, CE_WARN, "!%s() failed", func);
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}

static int
iwm_intr_setup(struct iwm_softc *sc)
{
	ENTERED;
	int intr_type;
	int ret;

	ret = ddi_intr_get_supported_types(sc->sc_dip, &intr_type);
	if (ret != DDI_SUCCESS) {
		dev_err(sc->sc_dip, CE_WARN,
		    "!ddi_intr_get_supported_types() failed");
		return (DDI_FAILURE);
	}

	if ((intr_type & DDI_INTR_TYPE_MSIX)) {
		if (iwm_intr_add(sc, DDI_INTR_TYPE_MSIX) == DDI_SUCCESS)
			return (DDI_SUCCESS);
		iwm_intr_teardown(sc);
	}

	if ((intr_type & DDI_INTR_TYPE_MSI)) {
		if (iwm_intr_add(sc, DDI_INTR_TYPE_MSI) == DDI_SUCCESS)
			return (DDI_SUCCESS);
		iwm_intr_teardown(sc);
	}

	if ((intr_type & DDI_INTR_TYPE_FIXED)) {
		if (iwm_intr_add(sc, DDI_INTR_TYPE_FIXED) == DDI_SUCCESS)
			return (DDI_SUCCESS);
		iwm_intr_teardown(sc);
	}

	dev_err(sc->sc_dip, CE_WARN, "!iwm_intr_setup() failed %d", intr_type);
	return (DDI_FAILURE);
}

int
iwm_poll_bit(struct iwm_softc *sc, int reg,
	uint32_t bits, uint32_t mask, int timo)
{
	ENTERED;
	for (;;) {
		if ((iwm_read(sc, reg) & mask) == (bits & mask)) {
			return 1;
		}
		if (timo < 10) {
			return 0;
		}
		timo -= 10;
		DELAY(10);
	}
}

#define IWM_HW_READY_TIMEOUT 50
int
iwm_set_hw_ready(struct iwm_softc *sc)
{
	ENTERED;
	int ready;

	IWM_SETBITS(sc, IWM_CSR_HW_IF_CONFIG_REG,
	    IWM_CSR_HW_IF_CONFIG_REG_BIT_NIC_READY);

	ready = iwm_poll_bit(sc, IWM_CSR_HW_IF_CONFIG_REG,
	    IWM_CSR_HW_IF_CONFIG_REG_BIT_NIC_READY,
	    IWM_CSR_HW_IF_CONFIG_REG_BIT_NIC_READY,
	    IWM_HW_READY_TIMEOUT);
	if (ready) {
		IWM_SETBITS(sc, IWM_CSR_MBOX_SET_REG,
		    IWM_CSR_MBOX_SET_REG_OS_ALIVE);
	}
	return ready;
}
#undef IWM_HW_READY_TIMEOUT

int
iwm_prepare_card_hw(struct iwm_softc *sc)
{
	int rv = 0;
	int t = 0;

	ENTERED;
	if (iwm_set_hw_ready(sc))
		goto out;

	DELAY(100);

	/* If HW is not ready, prepare the conditions to check again */
	IWM_SETBITS(sc, IWM_CSR_HW_IF_CONFIG_REG,
	    IWM_CSR_HW_IF_CONFIG_REG_PREPARE);

	do {
		if (iwm_set_hw_ready(sc))
			goto out;
		DELAY(200);
		t += 200;
	} while (t < 150000);

	rv = ETIMEDOUT;

 out:
	return rv;
}

static int
iwm_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	ENTERED;
	
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

	int instance = ddi_get_instance(dip);

	if (ddi_soft_state_zalloc(iwm_state, instance) != DDI_SUCCESS) {
		dev_err(dip, CE_WARN, "!ddi_soft_state_zalloc() failed");
		cmn_err(CE_NOTE, "instance = %d", instance);
		return (DDI_FAILURE);
	}

	struct iwm_softc *sc = ddi_get_soft_state(iwm_state, instance);
	ddi_set_driver_private(dip, (caddr_t)sc);
	
	struct ieee80211com *ic = &sc->sc_ic;

	sc->sc_dip = dip;

	if (pci_config_setup(dip, &sc->sc_pcih) != DDI_SUCCESS) {
		dev_err(sc->sc_dip, CE_WARN, "!pci_config_setup() failed");
		goto fail_pci_config;
	}

	/* Clear device-specific "PCI retry timeout" register (41h). */
	uint32_t reg = pci_config_get8(sc->sc_pcih, PCI_CFG_RETRY_TIMEOUT);
	if (reg)
		pci_config_put8(sc->sc_pcih, PCI_CFG_RETRY_TIMEOUT, 0);

	error = ddi_regs_map_setup(dip, 1, &sc->sc_base, 0, 0, &iwn_reg_accattr,
	    &sc->sc_regh);
	if (error != DDI_SUCCESS) {
		dev_err(sc->sc_dip, CE_WARN, "!ddi_regs_map_setup() failed");
		goto fail_regs_map;
	}

	/* Install interrupt handler. */
	if (iwm_intr_setup(sc) != DDI_SUCCESS)
		goto fail_intr;

	if (iwm_prepare_card_hw(sc) != 0) {
		dev_err(sc->sc_dip, CE_WARN, "could not initialize hardware\n");
		goto fail_hw;
	}
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

	sc->hw_type = iwm_read(sc, IWM_CSR_HW_REV);
	cmn_err(CE_NOTE, "sc->hw_type = %d", sc->hw_type);

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

fail_hw:
	iwm_intr_teardown(sc);
	
fail_intr:
	ddi_regs_map_free(&sc->sc_regh);
	
fail_regs_map:
fail_pci_capab:
	pci_config_teardown(&sc->sc_pcih);

fail_pci_config:
	ddi_soft_state_free(iwm_state, instance);

	return (DDI_FAILURE);
}

static int
iwm_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	struct iwm_softc *sc = ddi_get_driver_private(dip);
	ieee80211com_t *ic = &sc->sc_ic;

	cmn_err(CE_NOTE, "iwm_detach entered");
	switch (cmd) {
	case DDI_DETACH:
		break;
	default:
		return (DDI_FAILURE);
	}
	
	int error = mac_disable(ic->ic_mach);
	if (error != DDI_SUCCESS)
		return (error);
	mac_unregister(ic->ic_mach);
//	ieee80211_detach(ic);

	/* Uninstall interrupt handler. */
	iwm_intr_teardown(sc);

	ddi_regs_map_free(&sc->sc_regh);
	pci_config_teardown(&sc->sc_pcih);
	ddi_remove_minor_node(dip, NULL);
	ddi_soft_state_free(iwm_state, ddi_get_instance(dip));
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
