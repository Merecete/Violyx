/* RTL8125B/RTL8126 for Violyx. Adapted from the OpenBSD driver if_rge.c: the register sequences are
 * taken over unchanged. Left out are the Realtek firmware (MAC and PHY microcode) that OpenBSD
 * loads from a separate file, as well as interrupts, offloads and multiple queues. Violyx polls the
 * chip. */
/*
 * Copyright (c) 2019, 2020, 2023-2026
 *	Kevin Lo <kevlo@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifdef HOST
#include "rgehost.h"
#else
#include "os.h"
#endif
#define __packed __attribute__((packed))
struct rge_softc {
    u64 base;
    int rge_type, rge_flags;
    u16 rge_rcodever;
};

#define MII_BMCR             0
#define MII_ANAR             4
#define MII_100T2CR          9
#define BMCR_RESET           0x8000
#define BMCR_AUTOEN          0x1000
#define BMCR_STARTNEG        0x0200
#define BMCR_PDOWN           0x0800
#define ANAR_TX_FD           0x100
#define ANAR_TX              0x80
#define ANAR_10_FD           0x40
#define ANAR_10              0x20
#define ANAR_FC              0x400
#define ANAR_PAUSE_ASYM      0x800
#define GTCR_ADV_1000TFDX    0x200
#define GTCR_ADV_1000THDX    0x100
#define ETHER_HDR_LEN        14
#define ETHER_CRC_LEN        4
#define ETHER_VLAN_ENCAP_LEN 4
#define ETHER_ADDR_LEN       6
#define ETIMEDOUT            60
#define DELAY(n)             udelay(n)
#define printf(...)          ((void)0)
#ifndef HOST
#define IOR(sc, t, o)         (*(volatile t *)((sc)->base + (o)))
#define RGE_READ_1(sc, o)     IOR(sc, u8, o)
#define RGE_READ_2(sc, o)     IOR(sc, u16, o)
#define RGE_READ_4(sc, o)     IOR(sc, u32, o)
#define RGE_WRITE_1(sc, o, v) (IOR(sc, u8, o) = (u8)(v))
#define RGE_WRITE_2(sc, o, v) (IOR(sc, u16, o) = (u16)(v))
#define RGE_WRITE_4(sc, o, v) (IOR(sc, u32, o) = (u32)(v))
#endif
#define _KERNEL   1
#define nitems(a) (sizeof(a) / sizeof((a)[0]))
#include "rge_reg.h"
#define rge_phy_config_mcu(sc, v)     ((void)(v)) /* no firmware */
#define rge_ephy_config_mac_r25(sc)   ((void)0)
#define rge_ephy_config_mac_r27(sc)   ((void)0)
#define rge_phy_config_mac_r25(sc)    ((void)0)
#define rge_phy_config_mac_r25d_1(sc) ((void)0)
#define rge_phy_config_mac_r25d_2(sc) ((void)0)
#define rge_phy_config_mac_r27(sc)    ((void)0)
static void rge_ephy_config(struct rge_softc *);
static void rge_write_csi(struct rge_softc *sc, uint32_t reg, uint32_t val);
static uint32_t rge_read_csi(struct rge_softc *sc, uint32_t reg);
static void rge_write_mac_ocp(struct rge_softc *sc, uint16_t reg, uint16_t val);
static uint16_t rge_read_mac_ocp(struct rge_softc *sc, uint16_t reg);
static void rge_write_ephy(struct rge_softc *sc, uint16_t reg, uint16_t val);
static uint16_t rge_read_ephy(struct rge_softc *sc, uint16_t reg);
static void rge_write_phy(struct rge_softc *sc, uint16_t addr, uint16_t reg, uint16_t val);
static uint16_t rge_read_phy(struct rge_softc *sc, uint16_t addr, uint16_t reg);
static void rge_write_phy_ocp(struct rge_softc *sc, uint16_t reg, uint16_t val);
static uint16_t rge_read_phy_ocp(struct rge_softc *sc, uint16_t reg);
static int rge_reset(struct rge_softc *sc);
static int rge_exit_oob(struct rge_softc *sc);
static void rge_set_phy_power(struct rge_softc *sc, int on);
static void rge_hw_reset(struct rge_softc *sc);
static void rge_disable_aspm_clkreq(struct rge_softc *sc);
static void rge_patch_phy_mcu(struct rge_softc *sc, int set);
static void rge_ephy_config_mac_r25b(struct rge_softc *sc);
static void rge_phy_config_mac_r26_1(struct rge_softc *sc);
static void rge_phy_config_mac_r26_2(struct rge_softc *sc);
static void rge_phy_config_mac_r25b(struct rge_softc *sc);
static void rge_set_macaddr(struct rge_softc *sc, const uint8_t *addr);
static void rge_get_macaddr(struct rge_softc *sc, uint8_t *addr);
static int rge_phy_config(struct rge_softc *sc);
static void rge_write_csi(struct rge_softc *sc, uint32_t reg, uint32_t val) {
    int i;

    RGE_WRITE_4(sc, RGE_CSIDR, val);
    RGE_WRITE_4(sc, RGE_CSIAR,
                (reg & RGE_CSIAR_ADDR_MASK) | (RGE_CSIAR_BYTE_EN << RGE_CSIAR_BYTE_EN_SHIFT) |
                    RGE_CSIAR_BUSY);

    for (i = 0; i < 20000; i++) {
        DELAY(1);
        if (!(RGE_READ_4(sc, RGE_CSIAR) & RGE_CSIAR_BUSY))
            break;
    }

    DELAY(20);
}

static uint32_t rge_read_csi(struct rge_softc *sc, uint32_t reg) {
    int i;

    RGE_WRITE_4(sc, RGE_CSIAR,
                (reg & RGE_CSIAR_ADDR_MASK) | (RGE_CSIAR_BYTE_EN << RGE_CSIAR_BYTE_EN_SHIFT));

    for (i = 0; i < 20000; i++) {
        DELAY(1);
        if (RGE_READ_4(sc, RGE_CSIAR) & RGE_CSIAR_BUSY)
            break;
    }

    DELAY(20);

    return (RGE_READ_4(sc, RGE_CSIDR));
}

static void rge_write_mac_ocp(struct rge_softc *sc, uint16_t reg, uint16_t val) {
    uint32_t tmp;

    tmp = (reg >> 1) << RGE_MACOCP_ADDR_SHIFT;
    tmp += val;
    tmp |= RGE_MACOCP_BUSY;
    RGE_WRITE_4(sc, RGE_MACOCP, tmp);
}

static uint16_t rge_read_mac_ocp(struct rge_softc *sc, uint16_t reg) {
    uint32_t val;

    val = (reg >> 1) << RGE_MACOCP_ADDR_SHIFT;
    RGE_WRITE_4(sc, RGE_MACOCP, val);

    return (RGE_READ_4(sc, RGE_MACOCP) & RGE_MACOCP_DATA_MASK);
}

static void rge_write_ephy(struct rge_softc *sc, uint16_t reg, uint16_t val) {
    uint32_t tmp;
    int i;

    tmp = (reg & RGE_EPHYAR_ADDR_MASK) << RGE_EPHYAR_ADDR_SHIFT;
    tmp |= RGE_EPHYAR_BUSY | (val & RGE_EPHYAR_DATA_MASK);
    RGE_WRITE_4(sc, RGE_EPHYAR, tmp);

    for (i = 0; i < 20000; i++) {
        DELAY(1);
        if (!(RGE_READ_4(sc, RGE_EPHYAR) & RGE_EPHYAR_BUSY))
            break;
    }

    DELAY(20);
}

static uint16_t rge_read_ephy(struct rge_softc *sc, uint16_t reg) {
    uint32_t val;
    int i;

    val = (reg & RGE_EPHYAR_ADDR_MASK) << RGE_EPHYAR_ADDR_SHIFT;
    RGE_WRITE_4(sc, RGE_EPHYAR, val);

    for (i = 0; i < 20000; i++) {
        DELAY(1);
        val = RGE_READ_4(sc, RGE_EPHYAR);
        if (val & RGE_EPHYAR_BUSY)
            break;
    }

    DELAY(20);

    return (val & RGE_EPHYAR_DATA_MASK);
}

static void rge_write_phy(struct rge_softc *sc, uint16_t addr, uint16_t reg, uint16_t val) {
    uint16_t off, phyaddr;

    phyaddr = addr ? addr : RGE_PHYBASE + (reg / 8);
    phyaddr <<= 4;

    off = addr ? reg : 0x10 + (reg % 8);

    phyaddr += (off - 16) << 1;

    rge_write_phy_ocp(sc, phyaddr, val);
}

static uint16_t rge_read_phy(struct rge_softc *sc, uint16_t addr, uint16_t reg) {
    uint16_t off, phyaddr;

    phyaddr = addr ? addr : RGE_PHYBASE + (reg / 8);
    phyaddr <<= 4;

    off = addr ? reg : 0x10 + (reg % 8);

    phyaddr += (off - 16) << 1;

    return (rge_read_phy_ocp(sc, phyaddr));
}

static void rge_write_phy_ocp(struct rge_softc *sc, uint16_t reg, uint16_t val) {
    uint32_t tmp;
    int i;

    tmp = (reg >> 1) << RGE_PHYOCP_ADDR_SHIFT;
    tmp |= RGE_PHYOCP_BUSY | val;
    RGE_WRITE_4(sc, RGE_PHYOCP, tmp);

    for (i = 0; i < 20000; i++) {
        DELAY(1);
        if (!(RGE_READ_4(sc, RGE_PHYOCP) & RGE_PHYOCP_BUSY))
            break;
    }
}

static uint16_t rge_read_phy_ocp(struct rge_softc *sc, uint16_t reg) {
    uint32_t val;
    int i;

    val = (reg >> 1) << RGE_PHYOCP_ADDR_SHIFT;
    RGE_WRITE_4(sc, RGE_PHYOCP, val);

    for (i = 0; i < 20000; i++) {
        DELAY(1);
        val = RGE_READ_4(sc, RGE_PHYOCP);
        if (val & RGE_PHYOCP_BUSY)
            break;
    }

    return (val & RGE_PHYOCP_DATA_MASK);
}

static int rge_reset(struct rge_softc *sc) {
    int i;

    RGE_CLRBIT_4(sc, RGE_RXCFG,
                 RGE_RXCFG_ALLPHYS | RGE_RXCFG_INDIV | RGE_RXCFG_MULTI | RGE_RXCFG_BROAD |
                     RGE_RXCFG_RUNT | RGE_RXCFG_ERRPKT);

    /* Enable RXDV gate. */
    RGE_SETBIT_1(sc, RGE_PPSW, 0x08);

    RGE_SETBIT_1(sc, RGE_CMD, RGE_CMD_STOPREQ);
    DELAY(200);

    for (i = 0; i < 3000; i++) {
        DELAY(50);
        if ((RGE_READ_1(sc, RGE_MCUCMD) & (RGE_MCUCMD_RXFIFO_EMPTY | RGE_MCUCMD_TXFIFO_EMPTY)) ==
            (RGE_MCUCMD_RXFIFO_EMPTY | RGE_MCUCMD_TXFIFO_EMPTY))
            break;
    }
    if (sc->rge_type != MAC_R25) {
        for (i = 0; i < 3000; i++) {
            DELAY(50);
            if ((RGE_READ_2(sc, RGE_IM) & 0x0103) == 0x0103)
                break;
        }
    }

    RGE_WRITE_1(sc, RGE_CMD, RGE_READ_1(sc, RGE_CMD) & (RGE_CMD_TXENB | RGE_CMD_RXENB));

    /* Soft reset. */
    RGE_WRITE_1(sc, RGE_CMD, RGE_CMD_RESET);

    for (i = 0; i < RGE_TIMEOUT; i++) {
        DELAY(100);
        if (!(RGE_READ_1(sc, RGE_CMD) & RGE_CMD_RESET))
            break;
    }
    if (i == RGE_TIMEOUT) {
        printf("%s: reset never completed!\n", sc->sc_dev.dv_xname);
        return ETIMEDOUT;
    }

    return 0;
}

static int rge_exit_oob(struct rge_softc *sc) {
    int error, i;

    /* Disable RealWoW. */
    rge_write_mac_ocp(sc, 0xc0bc, 0x00ff);

    if ((error = rge_reset(sc)) != 0)
        return error;

    /* Disable OOB. */
    RGE_CLRBIT_1(sc, RGE_MCUCMD, RGE_MCUCMD_IS_OOB);

    RGE_MAC_CLRBIT(sc, 0xe8de, 0x4000);

    for (i = 0; i < 10; i++) {
        DELAY(100);
        if (RGE_READ_2(sc, RGE_TWICMD) & 0x0200)
            break;
    }

    rge_write_mac_ocp(sc, 0xc0aa, 0x07d0);
    rge_write_mac_ocp(sc, 0xc0a6, 0x01b5);
    rge_write_mac_ocp(sc, 0xc01e, 0x5555);

    for (i = 0; i < 10; i++) {
        DELAY(100);
        if (RGE_READ_2(sc, RGE_TWICMD) & 0x0200)
            break;
    }

    if (rge_read_mac_ocp(sc, 0xd42c) & 0x0100) {
        for (i = 0; i < RGE_TIMEOUT; i++) {
            if ((rge_read_phy_ocp(sc, 0xa420) & 0x0007) == 2)
                break;
            DELAY(1000);
        }
        RGE_MAC_CLRBIT(sc, 0xd42c, 0x0100);
        if (sc->rge_type != MAC_R25)
            RGE_PHY_CLRBIT(sc, 0xa466, 0x0001);
        RGE_PHY_CLRBIT(sc, 0xa468, 0x000a);
    }

    return 0;
}

static void rge_set_phy_power(struct rge_softc *sc, int on) {
    int i;

    if (on) {
        RGE_SETBIT_1(sc, RGE_PMCH, 0xc0);

        rge_write_phy(sc, 0, MII_BMCR, BMCR_AUTOEN);

        for (i = 0; i < RGE_TIMEOUT; i++) {
            if ((rge_read_phy_ocp(sc, 0xa420) & 0x0007) == 3)
                break;
            DELAY(1000);
        }
    } else {
        rge_write_phy(sc, 0, MII_BMCR, BMCR_AUTOEN | BMCR_PDOWN);
        RGE_CLRBIT_1(sc, RGE_PMCH, 0x80);
        RGE_CLRBIT_1(sc, RGE_PPSW, 0x40);
    }
}

static void rge_hw_reset(struct rge_softc *sc) {
    /* Disable interrupts */
    if (sc->rge_flags & RGE_FLAG_MULTIVEC) {
        RGE_WRITE_4(sc, RGE_IMR_CLR_V2, 0xffffffff);
        RGE_WRITE_4(sc, RGE_IMR_V4, 0xffffffff);
        RGE_WRITE_4(sc, RGE_ISR_V2, 0xffffffff);
        RGE_WRITE_4(sc, RGE_ISR_V4, 0xffffffff);
    } else {
        RGE_WRITE_4(sc, RGE_IMR, 0);
        RGE_WRITE_4(sc, RGE_ISR, RGE_READ_4(sc, RGE_ISR));
    }

    /* Clear timer interrupts. */
    RGE_WRITE_4(sc, RGE_TIMERINT0, 0);
    RGE_WRITE_4(sc, RGE_TIMERINT1, 0);
    RGE_WRITE_4(sc, RGE_TIMERINT2, 0);
    RGE_WRITE_4(sc, RGE_TIMERINT3, 0);

    rge_reset(sc);
}

static void rge_disable_aspm_clkreq(struct rge_softc *sc) {
    int unlock = 1;

    if ((RGE_READ_1(sc, RGE_EECMD) & RGE_EECMD_WRITECFG) == RGE_EECMD_WRITECFG)
        unlock = 0;

    if (unlock)
        RGE_SETBIT_1(sc, RGE_EECMD, RGE_EECMD_WRITECFG);

    if (RGE_TYPE_R26(sc) || RGE_TYPE_R27(sc))
        RGE_CLRBIT_1(sc, RGE_INT_CFG0, 0x08);
    else
        RGE_CLRBIT_1(sc, RGE_CFG2, RGE_CFG2_CLKREQ_EN);
    RGE_CLRBIT_1(sc, RGE_CFG5, RGE_CFG5_PME_STS);

    if (unlock)
        RGE_CLRBIT_1(sc, RGE_EECMD, RGE_EECMD_WRITECFG);
}

static void rge_patch_phy_mcu(struct rge_softc *sc, int set) {
    int i;

    if (set)
        RGE_PHY_SETBIT(sc, 0xb820, 0x0010);
    else
        RGE_PHY_CLRBIT(sc, 0xb820, 0x0010);

    for (i = 0; i < 1000; i++) {
        if (set) {
            if ((rge_read_phy_ocp(sc, 0xb800) & 0x0040) != 0)
                break;
        } else {
            if (!(rge_read_phy_ocp(sc, 0xb800) & 0x0040))
                break;
        }
        DELAY(100);
    }
    if (i == 1000)
        printf("%s: timeout waiting to patch phy mcu\n", sc->sc_dev.dv_xname);
}

static void rge_ephy_config_mac_r25b(struct rge_softc *sc) {
    int i;

    for (i = 0; i < nitems(mac_r25b_ephy); i++)
        rge_write_ephy(sc, mac_r25b_ephy[i].reg, mac_r25b_ephy[i].val);
}

static void rge_phy_config_mac_r26_1(struct rge_softc *sc) {
    uint16_t val;
    int i;
    static const uint16_t mac_cfg2_a438_value[] = {
        0x0044, 0x00a8, 0x00d6, 0x00ec, 0x00f6, 0x00fc, 0x00fe, 0x00fe, 0x00bc, 0x0058,
        0x002a, 0x003f, 0x3f02, 0x023c, 0x3b0a, 0x1c00, 0x0000, 0x0000, 0x0000, 0x0000};

    static const uint16_t mac_cfg2_b87e_value[] = {
        0x03ed, 0x03ff, 0x0009, 0x03fe, 0x000b, 0x0021, 0x03f7, 0x03b8, 0x03e0, 0x0049,
        0x0049, 0x03e0, 0x03b8, 0x03f7, 0x0021, 0x000b, 0x03fe, 0x0009, 0x03ff, 0x03ed,
        0x000e, 0x03fe, 0x03ed, 0x0006, 0x001a, 0x03f1, 0x03d8, 0x0023, 0x0054, 0x0322,
        0x00dd, 0x03ab, 0x03dc, 0x0027, 0x000e, 0x03e5, 0x03f9, 0x0012, 0x0001, 0x03f1};

    rge_phy_config_mcu(sc, RGE_MAC_R26_1_RCODE_VER);

    RGE_PHY_SETBIT(sc, 0xa442, 0x0800);
    rge_write_phy_ocp(sc, 0xa436, 0x80bf);
    val = rge_read_phy_ocp(sc, 0xa438) & ~0xff00;
    rge_write_phy_ocp(sc, 0xa438, val | 0xed00);
    rge_write_phy_ocp(sc, 0xa436, 0x80cd);
    val = rge_read_phy_ocp(sc, 0xa438) & ~0xff00;
    rge_write_phy_ocp(sc, 0xa438, val | 0x1000);
    rge_write_phy_ocp(sc, 0xa436, 0x80d1);
    val = rge_read_phy_ocp(sc, 0xa438) & ~0xff00;
    rge_write_phy_ocp(sc, 0xa438, val | 0xc800);
    rge_write_phy_ocp(sc, 0xa436, 0x80d4);
    val = rge_read_phy_ocp(sc, 0xa438) & ~0xff00;
    rge_write_phy_ocp(sc, 0xa438, val | 0xc800);
    rge_write_phy_ocp(sc, 0xa436, 0x80e1);
    rge_write_phy_ocp(sc, 0xa438, 0x10cc);
    rge_write_phy_ocp(sc, 0xa436, 0x80e5);
    rge_write_phy_ocp(sc, 0xa438, 0x4f0c);
    rge_write_phy_ocp(sc, 0xa436, 0x8387);
    val = rge_read_phy_ocp(sc, 0xa438) & ~0xff00;
    rge_write_phy_ocp(sc, 0xa438, val | 0x4700);
    val = rge_read_phy_ocp(sc, 0xa80c) & ~0x00c0;
    rge_write_phy_ocp(sc, 0xa80c, val | 0x0080);
    RGE_PHY_CLRBIT(sc, 0xac90, 0x0010);
    RGE_PHY_CLRBIT(sc, 0xad2c, 0x8000);
    rge_write_phy_ocp(sc, 0xb87c, 0x8321);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0x1100);
    RGE_PHY_SETBIT(sc, 0xacf8, 0x000c);
    rge_write_phy_ocp(sc, 0xa436, 0x8183);
    val = rge_read_phy_ocp(sc, 0xa438) & ~0xff00;
    rge_write_phy_ocp(sc, 0xa438, val | 0x5900);
    RGE_PHY_SETBIT(sc, 0xad94, 0x0020);
    RGE_PHY_CLRBIT(sc, 0xa654, 0x0800);
    RGE_PHY_SETBIT(sc, 0xb648, 0x4000);
    rge_write_phy_ocp(sc, 0xb87c, 0x839e);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0x2f00);
    rge_write_phy_ocp(sc, 0xb87c, 0x83f2);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0x0800);
    RGE_PHY_SETBIT(sc, 0xada0, 0x0002);
    rge_write_phy_ocp(sc, 0xb87c, 0x80f3);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0x9900);
    rge_write_phy_ocp(sc, 0xb87c, 0x8126);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0xc100);
    rge_write_phy_ocp(sc, 0xb87c, 0x893a);
    rge_write_phy_ocp(sc, 0xb87e, 0x8080);
    rge_write_phy_ocp(sc, 0xb87c, 0x8647);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0xe600);
    rge_write_phy_ocp(sc, 0xb87c, 0x862c);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0x1200);
    rge_write_phy_ocp(sc, 0xb87c, 0x864a);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0xe600);
    rge_write_phy_ocp(sc, 0xb87c, 0x80a0);
    rge_write_phy_ocp(sc, 0xb87e, 0xbcbc);
    rge_write_phy_ocp(sc, 0xb87c, 0x805e);
    rge_write_phy_ocp(sc, 0xb87e, 0xbcbc);
    rge_write_phy_ocp(sc, 0xb87c, 0x8056);
    rge_write_phy_ocp(sc, 0xb87e, 0x3077);
    rge_write_phy_ocp(sc, 0xb87c, 0x8058);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0x5a00);
    rge_write_phy_ocp(sc, 0xb87c, 0x8098);
    rge_write_phy_ocp(sc, 0xb87e, 0x3077);
    rge_write_phy_ocp(sc, 0xb87c, 0x809a);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0x5a00);
    rge_write_phy_ocp(sc, 0xb87c, 0x8052);
    rge_write_phy_ocp(sc, 0xb87e, 0x3733);
    rge_write_phy_ocp(sc, 0xb87c, 0x8094);
    rge_write_phy_ocp(sc, 0xb87e, 0x3733);
    rge_write_phy_ocp(sc, 0xb87c, 0x807f);
    rge_write_phy_ocp(sc, 0xb87e, 0x7c75);
    rge_write_phy_ocp(sc, 0xb87c, 0x803d);
    rge_write_phy_ocp(sc, 0xb87e, 0x7c75);
    rge_write_phy_ocp(sc, 0xb87c, 0x8036);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0x3000);
    rge_write_phy_ocp(sc, 0xb87c, 0x8078);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0x3000);
    rge_write_phy_ocp(sc, 0xb87c, 0x8031);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0x3300);
    rge_write_phy_ocp(sc, 0xb87c, 0x8073);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0x3300);
    val = rge_read_phy_ocp(sc, 0xae06) & ~0xfc00;
    rge_write_phy_ocp(sc, 0xae06, val | 0x7c00);
    rge_write_phy_ocp(sc, 0xb87c, 0x89D1);
    rge_write_phy_ocp(sc, 0xb87e, 0x0004);
    rge_write_phy_ocp(sc, 0xa436, 0x8fbd);
    val = rge_read_phy_ocp(sc, 0xa438) & ~0xff00;
    rge_write_phy_ocp(sc, 0xa438, val | 0x0a00);
    rge_write_phy_ocp(sc, 0xa436, 0x8fbe);
    rge_write_phy_ocp(sc, 0xa438, 0x0d09);
    rge_write_phy_ocp(sc, 0xb87c, 0x89cd);
    rge_write_phy_ocp(sc, 0xb87e, 0x0f0f);
    rge_write_phy_ocp(sc, 0xb87c, 0x89cf);
    rge_write_phy_ocp(sc, 0xb87e, 0x0f0f);
    rge_write_phy_ocp(sc, 0xb87c, 0x83a4);
    rge_write_phy_ocp(sc, 0xb87e, 0x6600);
    rge_write_phy_ocp(sc, 0xb87c, 0x83a6);
    rge_write_phy_ocp(sc, 0xb87e, 0x6601);
    rge_write_phy_ocp(sc, 0xb87c, 0x83c0);
    rge_write_phy_ocp(sc, 0xb87e, 0x6600);
    rge_write_phy_ocp(sc, 0xb87c, 0x83c2);
    rge_write_phy_ocp(sc, 0xb87e, 0x6601);
    rge_write_phy_ocp(sc, 0xb87c, 0x8414);
    rge_write_phy_ocp(sc, 0xb87e, 0x6600);
    rge_write_phy_ocp(sc, 0xb87c, 0x8416);
    rge_write_phy_ocp(sc, 0xb87e, 0x6601);
    rge_write_phy_ocp(sc, 0xb87c, 0x83f8);
    rge_write_phy_ocp(sc, 0xb87e, 0x6600);
    rge_write_phy_ocp(sc, 0xb87c, 0x83fa);
    rge_write_phy_ocp(sc, 0xb87e, 0x6601);

    rge_patch_phy_mcu(sc, 1);
    val = rge_read_phy_ocp(sc, 0xbd96) & ~0x1f00;
    rge_write_phy_ocp(sc, 0xbd96, val | 0x1000);
    val = rge_read_phy_ocp(sc, 0xbf1c) & ~0x0007;
    rge_write_phy_ocp(sc, 0xbf1c, val | 0x0007);
    RGE_PHY_CLRBIT(sc, 0xbfbe, 0x8000);
    val = rge_read_phy_ocp(sc, 0xbf40) & ~0x0380;
    rge_write_phy_ocp(sc, 0xbf40, val | 0x0280);
    val = rge_read_phy_ocp(sc, 0xbf90) & ~0x0080;
    rge_write_phy_ocp(sc, 0xbf90, val | 0x0060);
    val = rge_read_phy_ocp(sc, 0xbf90) & ~0x0010;
    rge_write_phy_ocp(sc, 0xbf90, val | 0x000c);
    rge_patch_phy_mcu(sc, 0);

    rge_write_phy_ocp(sc, 0xa436, 0x843b);
    val = rge_read_phy_ocp(sc, 0xa438) & ~0xff00;
    rge_write_phy_ocp(sc, 0xa438, val | 0x2000);
    rge_write_phy_ocp(sc, 0xa436, 0x843d);
    val = rge_read_phy_ocp(sc, 0xa438) & ~0xff00;
    rge_write_phy_ocp(sc, 0xa438, val | 0x2000);
    RGE_PHY_CLRBIT(sc, 0xb516, 0x007f);
    RGE_PHY_CLRBIT(sc, 0xbf80, 0x0030);

    rge_write_phy_ocp(sc, 0xa436, 0x8188);
    for (i = 0; i < 11; i++)
        rge_write_phy_ocp(sc, 0xa438, mac_cfg2_a438_value[i]);

    rge_write_phy_ocp(sc, 0xb87c, 0x8015);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0x0800);
    rge_write_phy_ocp(sc, 0xb87c, 0x8ffd);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0);
    rge_write_phy_ocp(sc, 0xb87c, 0x8fff);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0x7f00);
    rge_write_phy_ocp(sc, 0xb87c, 0x8ffb);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0x0100);
    rge_write_phy_ocp(sc, 0xb87c, 0x8fe9);
    rge_write_phy_ocp(sc, 0xb87e, 0x0002);
    rge_write_phy_ocp(sc, 0xb87c, 0x8fef);
    rge_write_phy_ocp(sc, 0xb87e, 0x00a5);
    rge_write_phy_ocp(sc, 0xb87c, 0x8ff1);
    rge_write_phy_ocp(sc, 0xb87e, 0x0106);
    rge_write_phy_ocp(sc, 0xb87c, 0x8fe1);
    rge_write_phy_ocp(sc, 0xb87e, 0x0102);
    rge_write_phy_ocp(sc, 0xb87c, 0x8fe3);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0x0400);
    RGE_PHY_SETBIT(sc, 0xa654, 0x0800);
    RGE_PHY_CLRBIT(sc, 0xa654, 0x0003);
    rge_write_phy_ocp(sc, 0xac3a, 0x5851);
    val = rge_read_phy_ocp(sc, 0xac3c) & ~0xd000;
    rge_write_phy_ocp(sc, 0xac3c, val | 0x2000);
    val = rge_read_phy_ocp(sc, 0xac42) & ~0x0200;
    rge_write_phy_ocp(sc, 0xac42, val | 0x01c0);
    RGE_PHY_CLRBIT(sc, 0xac3e, 0xe000);
    RGE_PHY_CLRBIT(sc, 0xac42, 0x0038);
    val = rge_read_phy_ocp(sc, 0xac42) & ~0x0002;
    rge_write_phy_ocp(sc, 0xac42, val | 0x0005);
    rge_write_phy_ocp(sc, 0xac1a, 0x00db);
    rge_write_phy_ocp(sc, 0xade4, 0x01b5);
    RGE_PHY_CLRBIT(sc, 0xad9c, 0x0c00);
    rge_write_phy_ocp(sc, 0xb87c, 0x814b);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0x1100);
    rge_write_phy_ocp(sc, 0xb87c, 0x814d);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0x1100);
    rge_write_phy_ocp(sc, 0xb87c, 0x814f);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0x0b00);
    rge_write_phy_ocp(sc, 0xb87c, 0x8142);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0x0100);
    rge_write_phy_ocp(sc, 0xb87c, 0x8144);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0x0100);
    rge_write_phy_ocp(sc, 0xb87c, 0x8150);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0x0100);
    rge_write_phy_ocp(sc, 0xb87c, 0x8118);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0x0700);
    rge_write_phy_ocp(sc, 0xb87c, 0x811a);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0x0700);
    rge_write_phy_ocp(sc, 0xb87c, 0x811c);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0x0500);
    rge_write_phy_ocp(sc, 0xb87c, 0x810f);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0x0100);
    rge_write_phy_ocp(sc, 0xb87c, 0x8111);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0x0100);
    rge_write_phy_ocp(sc, 0xb87c, 0x811d);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0x0100);
    RGE_PHY_SETBIT(sc, 0xac36, 0x1000);
    RGE_PHY_CLRBIT(sc, 0xad1c, 0x0100);
    val = rge_read_phy_ocp(sc, 0xade8) & ~0xffc0;
    rge_write_phy_ocp(sc, 0xade8, val | 0x1400);
    rge_write_phy_ocp(sc, 0xb87c, 0x864b);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0x9d00);

    rge_write_phy_ocp(sc, 0xa436, 0x8f97);
    for (; i < nitems(mac_cfg2_a438_value); i++)
        rge_write_phy_ocp(sc, 0xa438, mac_cfg2_a438_value[i]);

    RGE_PHY_SETBIT(sc, 0xad9c, 0x0020);
    rge_write_phy_ocp(sc, 0xb87c, 0x8122);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0x0c00);

    rge_write_phy_ocp(sc, 0xb87c, 0x82c8);
    for (i = 0; i < 20; i++)
        rge_write_phy_ocp(sc, 0xb87e, mac_cfg2_b87e_value[i]);

    rge_write_phy_ocp(sc, 0xb87c, 0x80ef);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0x0c00);

    rge_write_phy_ocp(sc, 0xb87c, 0x82a0);
    for (; i < nitems(mac_cfg2_b87e_value); i++)
        rge_write_phy_ocp(sc, 0xb87e, mac_cfg2_b87e_value[i]);

    rge_write_phy_ocp(sc, 0xa436, 0x8018);
    RGE_PHY_SETBIT(sc, 0xa438, 0x2000);
    rge_write_phy_ocp(sc, 0xb87c, 0x8fe4);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0);
    val = rge_read_phy_ocp(sc, 0xb54c) & ~0xffc0;
    rge_write_phy_ocp(sc, 0xb54c, val | 0x3700);
}

static void rge_phy_config_mac_r26_2(struct rge_softc *sc) {
    uint16_t val;
    int i;
    static const uint16_t mac_cfg3_b87e_value[] = {
        0x03ed, 0x03ff, 0x0009, 0x03fe, 0x000b, 0x0021, 0x03f7, 0x03b8, 0x03e0, 0x0049, 0x0049,
        0x03e0, 0x03b8, 0x03f7, 0x0021, 0x000b, 0x03fe, 0x0009, 0x03ff, 0x03ed, 0x82a0, 0x000e,
        0x03fe, 0x03ed, 0x0006, 0x001a, 0x03f1, 0x03d8, 0x0023, 0x0054, 0x0322, 0x00dd, 0x03ab,
        0x03dc, 0x0027, 0x000e, 0x03e5, 0x03f9, 0x0012, 0x0001, 0x03f1};

    rge_phy_config_mcu(sc, RGE_MAC_R26_2_RCODE_VER);

    RGE_PHY_SETBIT(sc, 0xa442, 0x0800);
    rge_write_phy_ocp(sc, 0xa436, 0x8183);
    val = rge_read_phy_ocp(sc, 0xa438) & ~0xff00;
    rge_write_phy_ocp(sc, 0xa438, val | 0x5900);
    RGE_PHY_SETBIT(sc, 0xa654, 0x0800);
    RGE_PHY_SETBIT(sc, 0xb648, 0x4000);
    RGE_PHY_SETBIT(sc, 0xad2c, 0x8000);
    RGE_PHY_SETBIT(sc, 0xad94, 0x0020);
    RGE_PHY_SETBIT(sc, 0xada0, 0x0002);
    val = rge_read_phy_ocp(sc, 0xae06) & ~0xfc00;
    rge_write_phy_ocp(sc, 0xae06, val | 0x7c00);
    rge_write_phy_ocp(sc, 0xb87c, 0x8647);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0xe600);
    rge_write_phy_ocp(sc, 0xb87c, 0x8036);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0x3000);
    rge_write_phy_ocp(sc, 0xb87c, 0x8078);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0x3000);
    rge_write_phy_ocp(sc, 0xb87c, 0x89e9);
    RGE_PHY_SETBIT(sc, 0xb87e, 0xff00);
    rge_write_phy_ocp(sc, 0xb87c, 0x8ffd);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0x0100);
    rge_write_phy_ocp(sc, 0xb87c, 0x8ffe);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0x0200);
    rge_write_phy_ocp(sc, 0xb87c, 0x8fff);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0x0400);
    rge_write_phy_ocp(sc, 0xa436, 0x8018);
    val = rge_read_phy_ocp(sc, 0xa438) & ~0xff00;
    rge_write_phy_ocp(sc, 0xa438, val | 0x7700);
    rge_write_phy_ocp(sc, 0xa436, 0x8f9c);
    rge_write_phy_ocp(sc, 0xa438, 0x0005);
    rge_write_phy_ocp(sc, 0xa438, 0x0000);
    rge_write_phy_ocp(sc, 0xa438, 0x00ed);
    rge_write_phy_ocp(sc, 0xa438, 0x0502);
    rge_write_phy_ocp(sc, 0xa438, 0x0b00);
    rge_write_phy_ocp(sc, 0xa438, 0xd401);
    rge_write_phy_ocp(sc, 0xa436, 0x8fa8);
    val = rge_read_phy_ocp(sc, 0xa438) & ~0xff00;
    rge_write_phy_ocp(sc, 0xa438, val | 0x2900);
    rge_write_phy_ocp(sc, 0xb87c, 0x814b);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0x1100);
    rge_write_phy_ocp(sc, 0xb87c, 0x814d);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0x1100);
    rge_write_phy_ocp(sc, 0xb87c, 0x814f);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0x0b00);
    rge_write_phy_ocp(sc, 0xb87c, 0x8142);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0x0100);
    rge_write_phy_ocp(sc, 0xb87c, 0x8144);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0x0100);
    rge_write_phy_ocp(sc, 0xb87c, 0x8150);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0x0100);
    rge_write_phy_ocp(sc, 0xb87c, 0x8118);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0x0700);
    rge_write_phy_ocp(sc, 0xb87c, 0x811a);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0x0700);
    rge_write_phy_ocp(sc, 0xb87c, 0x811c);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0x0500);
    rge_write_phy_ocp(sc, 0xb87c, 0x810f);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0x0100);
    rge_write_phy_ocp(sc, 0xb87c, 0x8111);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0x0100);
    rge_write_phy_ocp(sc, 0xb87c, 0x811d);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0x0100);
    RGE_PHY_SETBIT(sc, 0xad1c, 0x0100);
    val = rge_read_phy_ocp(sc, 0xade8) & ~0xffc0;
    rge_write_phy_ocp(sc, 0xade8, val | 0x1400);
    rge_write_phy_ocp(sc, 0xb87c, 0x864b);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0x9d00);
    rge_write_phy_ocp(sc, 0xb87c, 0x862c);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0x1200);
    rge_write_phy_ocp(sc, 0xa436, 0x8566);
    rge_write_phy_ocp(sc, 0xa438, 0x003f);
    rge_write_phy_ocp(sc, 0xa438, 0x3f02);
    rge_write_phy_ocp(sc, 0xa438, 0x023c);
    rge_write_phy_ocp(sc, 0xa438, 0x3b0a);
    rge_write_phy_ocp(sc, 0xa438, 0x1c00);
    rge_write_phy_ocp(sc, 0xa438, 0x0000);
    rge_write_phy_ocp(sc, 0xa438, 0x0000);
    rge_write_phy_ocp(sc, 0xa438, 0x0000);
    rge_write_phy_ocp(sc, 0xa438, 0x0000);
    RGE_PHY_SETBIT(sc, 0xad9c, 0x0020);
    rge_write_phy_ocp(sc, 0xb87c, 0x8122);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0x0c00);
    rge_write_phy_ocp(sc, 0xb87c, 0x82c8);
    for (i = 0; i < 20; i++)
        rge_write_phy_ocp(sc, 0xb87e, mac_cfg3_b87e_value[i]);
    rge_write_phy_ocp(sc, 0xb87c, 0x80ef);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0x0c00);
    for (; i < nitems(mac_cfg3_b87e_value); i++)
        rge_write_phy_ocp(sc, 0xb87e, mac_cfg3_b87e_value[i]);
    RGE_PHY_SETBIT(sc, 0xa430, 0x0003);
    val = rge_read_phy_ocp(sc, 0xb54c) & ~0xffc0;
    rge_write_phy_ocp(sc, 0xb54c, val | 0x3700);
    RGE_PHY_SETBIT(sc, 0xb648, 0x0040);
    rge_write_phy_ocp(sc, 0xb87c, 0x8082);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0x5d00);
    rge_write_phy_ocp(sc, 0xb87c, 0x807c);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0x5000);
    rge_write_phy_ocp(sc, 0xb87c, 0x809d);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0x5000);
}

static void rge_phy_config_mac_r25b(struct rge_softc *sc) {
    uint16_t val;
    int i;

    rge_phy_config_mcu(sc, RGE_MAC_R25B_RCODE_VER);

    RGE_PHY_SETBIT(sc, 0xa442, 0x0800);
    val = rge_read_phy_ocp(sc, 0xac46) & ~0x00f0;
    rge_write_phy_ocp(sc, 0xac46, val | 0x0090);
    val = rge_read_phy_ocp(sc, 0xad30) & ~0x0003;
    rge_write_phy_ocp(sc, 0xad30, val | 0x0001);
    rge_write_phy_ocp(sc, 0xb87c, 0x80f5);
    rge_write_phy_ocp(sc, 0xb87e, 0x760e);
    rge_write_phy_ocp(sc, 0xb87c, 0x8107);
    rge_write_phy_ocp(sc, 0xb87e, 0x360e);
    rge_write_phy_ocp(sc, 0xb87c, 0x8551);
    val = rge_read_phy_ocp(sc, 0xb87e) & ~0xff00;
    rge_write_phy_ocp(sc, 0xb87e, val | 0x0800);
    val = rge_read_phy_ocp(sc, 0xbf00) & ~0xe000;
    rge_write_phy_ocp(sc, 0xbf00, val | 0xa000);
    val = rge_read_phy_ocp(sc, 0xbf46) & ~0x0f00;
    rge_write_phy_ocp(sc, 0xbf46, val | 0x0300);
    for (i = 0; i < 10; i++) {
        rge_write_phy_ocp(sc, 0xa436, 0x8044 + i * 6);
        rge_write_phy_ocp(sc, 0xa438, 0x2417);
    }
    RGE_PHY_SETBIT(sc, 0xa4ca, 0x0040);
    val = rge_read_phy_ocp(sc, 0xbf84) & ~0xe000;
    rge_write_phy_ocp(sc, 0xbf84, val | 0xa000);
    rge_write_phy_ocp(sc, 0xa436, 0x8170);
    val = rge_read_phy_ocp(sc, 0xa438) & ~0x2700;
    rge_write_phy_ocp(sc, 0xa438, val | 0xd800);
    RGE_PHY_SETBIT(sc, 0xa424, 0x0008);
}

static void rge_set_macaddr(struct rge_softc *sc, const uint8_t *addr) {
    RGE_SETBIT_1(sc, RGE_EECMD, RGE_EECMD_WRITECFG);
    RGE_WRITE_4(sc, RGE_MAC0, addr[3] << 24 | addr[2] << 16 | addr[1] << 8 | addr[0]);
    RGE_WRITE_4(sc, RGE_MAC4, addr[5] << 8 | addr[4]);
    RGE_CLRBIT_1(sc, RGE_EECMD, RGE_EECMD_WRITECFG);
}

static void rge_get_macaddr(struct rge_softc *sc, uint8_t *addr) {
    int i;

    for (i = 0; i < ETHER_ADDR_LEN; i++)
        addr[i] = RGE_READ_1(sc, RGE_MAC0 + i);

    *(uint32_t *)&addr[0] = RGE_READ_4(sc, RGE_ADDR0);
    *(uint16_t *)&addr[4] = RGE_READ_2(sc, RGE_ADDR1);

    rge_set_macaddr(sc, addr);
}

static int rge_phy_config(struct rge_softc *sc) {
    uint16_t val = 0;
    int i;

    rge_ephy_config(sc);

    /* PHY reset. */
    rge_write_phy(sc, 0, MII_ANAR,
                  rge_read_phy(sc, 0, MII_ANAR) & ~(ANAR_TX_FD | ANAR_TX | ANAR_10_FD | ANAR_10));
    rge_write_phy(sc, 0, MII_100T2CR,
                  rge_read_phy(sc, 0, MII_100T2CR) & ~(GTCR_ADV_1000TFDX | GTCR_ADV_1000THDX));
    switch (sc->rge_type) {
        case MAC_R27:
        case MAC_R27ATF:
            val |= RGE_ADV_10000TFDX;
            /* fallthrough */
        case MAC_R26_1:
        case MAC_R26_2:
            val |= RGE_ADV_5000TFDX;
            /* fallthrough */
        default:
            val |= RGE_ADV_2500TFDX;
            break;
    }
    RGE_PHY_CLRBIT(sc, 0xa5d4, val);
    rge_write_phy(sc, 0, MII_BMCR, BMCR_RESET | BMCR_AUTOEN | BMCR_STARTNEG);
    for (i = 0; i < 2500; i++) {
        if (!(rge_read_phy(sc, 0, MII_BMCR) & BMCR_RESET))
            break;
        DELAY(1000);
    }
    if (i == 2500) {
        printf("%s: PHY reset failed\n", sc->sc_dev.dv_xname);
        return (ETIMEDOUT);
    }

    /* Read ram code version. */
    rge_write_phy_ocp(sc, 0xa436, 0x801e);
    sc->rge_rcodever = rge_read_phy_ocp(sc, 0xa438);

    switch (sc->rge_type) {
        case MAC_R25:
            rge_phy_config_mac_r25(sc);
            break;
        case MAC_R25B:
            rge_phy_config_mac_r25b(sc);
            break;
        case MAC_R25D_1:
            rge_phy_config_mac_r25d_1(sc);
            break;
        case MAC_R25D_2:
            rge_phy_config_mac_r25d_2(sc);
            break;
        case MAC_R26_1:
            rge_phy_config_mac_r26_1(sc);
            break;
        case MAC_R26_2:
            rge_phy_config_mac_r26_2(sc);
            break;
        case MAC_R27:
        case MAC_R27ATF:
            rge_phy_config_mac_r27(sc);
            break;
        default:
            break; /* Can't happen. */
    }

    RGE_PHY_CLRBIT(sc, 0xa5b4, 0x8000);

    /* Disable EEE. */
    RGE_MAC_CLRBIT(sc, 0xe040, 0x0003);
    if (sc->rge_type == MAC_R25) {
        RGE_MAC_CLRBIT(sc, 0xeb62, 0x0006);
        RGE_PHY_CLRBIT(sc, 0xa432, 0x0010);
    } else if (sc->rge_type == MAC_R25B || RGE_TYPE_R25D(sc))
        RGE_PHY_SETBIT(sc, 0xa432, 0x0010);

    RGE_PHY_CLRBIT(sc, 0xa5d0, RGE_TYPE_R27(sc) ? 0x000e : 0x0006);
    if (RGE_TYPE_R26(sc) || RGE_TYPE_R27(sc))
        RGE_PHY_CLRBIT(sc, 0xa6d4, 0x0003);
    else
        RGE_PHY_CLRBIT(sc, 0xa6d4, 0x0001);
    RGE_PHY_CLRBIT(sc, 0xa6d8, 0x0010);
    RGE_PHY_CLRBIT(sc, 0xa428, 0x0080);
    RGE_PHY_CLRBIT(sc, 0xa4a2, 0x0200);

    /* Disable advanced EEE. */
    RGE_MAC_CLRBIT(sc, 0xe052, 0x0001);
    RGE_PHY_CLRBIT(sc, 0xa442, 0x3000);
    RGE_PHY_CLRBIT(sc, 0xa430, 0x8000);

    return (0);
}

static void rge_ephy_config(struct rge_softc *sc) {
    if (sc->rge_type == MAC_R25B)
        rge_ephy_config_mac_r25b(sc);
}

/* ---------- Violyx: rings, start, sending and receiving ---------- */
#define NRX   128
#define NTX   32
#define BUFSZ 2048
static struct rge_softc S;
static struct rge_rx_desc *rl;
static struct rge_tx_desc *tl;
static u8 *rb, *tb;
static int rxi, txi, stage;
static u32 hwrev;
static u64 txok, rxok, rxerr, txbusy;
static u32 mask_rd(u32 v) {
    return v;
}

static void rx_arm(int i) {
    struct rge_rx_desc *r = &rl[i];
    u32 c = BUFSZ | (i == NRX - 1 ? RGE_RDCMDSTS_EOR : 0);
    r->hi_qword0.rge_addr = phys(rb + i * BUFSZ);
    r->hi_qword1.rx_qword4.rge_extsts = 0;
    r->hi_qword1.rx_qword4.rge_cmdsts = c;
    __asm__ volatile("mfence" : : : "memory");
    r->hi_qword1.rx_qword4.rge_cmdsts = c | RGE_RDCMDSTS_OWN;
}

static int chip_start(const u8 *mac) {
    struct rge_softc *sc = &S;
    u32 v;
    stage = 2;
    if (rge_exit_oob(sc))
        return -1;
    stage = 3;
    rge_set_phy_power(sc, 1);
    rge_disable_aspm_clkreq(sc);
    RGE_CLRBIT_1(sc, 0xf1, 0x80);
    RGE_MAC_CLRBIT(sc, 0xd40a, 0x0010); /* UPS off */
    rge_write_mac_ocp(sc, 0xfc48, 0);   /* MAC MCU off; without firmware it stays off */
    for (u16 reg = 0xfc28; reg < 0xfc48; reg += 2)
        rge_write_mac_ocp(sc, reg, 0);
    DELAY(3000);
    rge_write_mac_ocp(sc, 0xfc26, 0);
    rge_write_csi(sc, 0x108, rge_read_csi(sc, 0x108) | 0x00100000);
    rge_hw_reset(sc);
    stage = 4;
    if (rge_phy_config(sc))
        return -2;
    stage = 5;
    rge_set_macaddr(sc, mac);
    RGE_SETBIT_1(sc, RGE_EECMD, RGE_EECMD_WRITECFG);
    RGE_CLRBIT_1(sc, 0xf1, 0x80);
    rge_disable_aspm_clkreq(sc);
    RGE_WRITE_2(sc, RGE_EEE_TXIDLE_TIMER, RGE_JUMBO_MTU + ETHER_HDR_LEN + 32);
    RGE_WRITE_4(sc, RGE_RXDESC_ADDR_LO, RGE_ADDR_LO(phys(rl)));
    RGE_WRITE_4(sc, RGE_RXDESC_ADDR_HI, RGE_ADDR_HI(phys(rl)));
    RGE_WRITE_4(sc, RGE_TXDESC_ADDR_LO, RGE_ADDR_LO(phys(tl)));
    RGE_WRITE_4(sc, RGE_TXDESC_ADDR_HI, RGE_ADDR_HI(phys(tl)));
    RGE_WRITE_4(sc, RGE_RXCFG,
                sc->rge_type == MAC_R25B ? RGE_RXCFG_CONFIG_8125B : RGE_RXCFG_CONFIG_8126);
    RGE_WRITE_4(sc, RGE_TXCFG, RGE_TXCFG_CONFIG);
    v = rge_read_csi(sc, 0x70c) & ~0x3f000000;
    rge_write_csi(sc, 0x70c, v | 0x27000000);
    if (RGE_TYPE_R26(sc))
        rge_write_csi(sc, 0x890, rge_read_csi(sc, 0x890) & ~0x00000001);
    else
        RGE_WRITE_2(sc, 0x0382, 0x221b);
    RGE_WRITE_4(sc, RGE_RSS_CTRL, 0);
    RGE_WRITE_2(sc, RGE_RXQUEUE_CTRL, RGE_READ_2(sc, RGE_RXQUEUE_CTRL) & ~0x001c);
    RGE_CLRBIT_1(sc, RGE_CFG1, RGE_CFG1_SPEED_DOWN);
    rge_write_mac_ocp(sc, 0xc140, 0xffff);
    rge_write_mac_ocp(sc, 0xc142, 0xffff);
    RGE_MAC_SETBIT(sc, 0xeb58, 0x0001);
    if (RGE_TYPE_R26(sc))
        RGE_CLRBIT_1(sc, 0xd8, 0x02);
    v = rge_read_mac_ocp(sc, 0xe614) & ~0x0700;
    rge_write_mac_ocp(sc, 0xe614, v | (sc->rge_type == MAC_R25B ? 0x0200 : 0x0300));
    rge_write_mac_ocp(sc, 0xe63e, rge_read_mac_ocp(sc, 0xe63e) & ~0x0c00);
    v = rge_read_mac_ocp(sc, 0xe63e) & ~0x0030;
    rge_write_mac_ocp(sc, 0xe63e, v | 0x0020);
    RGE_MAC_CLRBIT(sc, 0xc0b4, 0x0001);
    RGE_MAC_SETBIT(sc, 0xc0b4, 0x0001);
    RGE_MAC_SETBIT(sc, 0xc0b4, 0x000c);
    v = rge_read_mac_ocp(sc, 0xeb6a) & ~0x00ff;
    rge_write_mac_ocp(sc, 0xeb6a, v | 0x0033);
    v = rge_read_mac_ocp(sc, 0xeb50) & ~0x03e0;
    rge_write_mac_ocp(sc, 0xeb50, v | 0x0040);
    RGE_MAC_CLRBIT(sc, 0xe056, 0x00f0);
    RGE_WRITE_1(sc, RGE_TDFNR, 0x10);
    RGE_MAC_CLRBIT(sc, 0xe040, 0x1000);
    v = rge_read_mac_ocp(sc, 0xea1c) & ~0x0003;
    rge_write_mac_ocp(sc, 0xea1c, v | 0x0001);
    rge_write_mac_ocp(sc, 0xe0c0, 0x4000);
    RGE_MAC_SETBIT(sc, 0xe052, 0x0060);
    RGE_MAC_CLRBIT(sc, 0xe052, 0x0088);
    v = rge_read_mac_ocp(sc, 0xd430) & ~0x0fff;
    rge_write_mac_ocp(sc, 0xd430, v | 0x045f);
    RGE_SETBIT_1(sc, RGE_DLLPR, RGE_DLLPR_PFM_EN | RGE_DLLPR_TX_10M_PS_EN);
    RGE_SETBIT_1(sc, RGE_PPSW, 0x40);
    RGE_MAC_CLRBIT(sc, 0xe080, 0x0002); /* EEE plus off */
    if (RGE_TYPE_R26(sc))
        RGE_MAC_CLRBIT(sc, 0xea1c, 0x0304);
    else
        RGE_MAC_CLRBIT(sc, 0xea1c, 0x0004);
    RGE_MAC_SETBIT(sc, 0xeb54, 0x0001);
    DELAY(1);
    RGE_MAC_CLRBIT(sc, 0xeb54, 0x0001);
    RGE_CLRBIT_2(sc, 0x1880, 0x0030);
    RGE_CLRBIT_1(sc, RGE_INT_CFG0, RGE_INT_CFG0_EN);
    RGE_WRITE_4(sc, RGE_TIMERINT0, 0);
    RGE_WRITE_4(sc, RGE_TIMERINT1, 0);
    RGE_WRITE_4(sc, RGE_TIMERINT2, 0);
    RGE_WRITE_4(sc, RGE_TIMERINT3, 0);
    for (int i = 0; i < 32; i++)
        RGE_WRITE_4(sc, RGE_INTMITI(i), 0);
    if (RGE_TYPE_R26(sc)) {
        RGE_CLRBIT_1(sc, RGE_INT_CFG0,
                     RGE_INT_CFG0_TIMEOUT_BYPASS | RGE_INT_CFG0_RDU_BYPASS_8126 |
                         RGE_INT_CFG0_MITIGATION_BYPASS);
        RGE_WRITE_2(sc, RGE_INT_CFG1, 0);
    }
    RGE_MAC_SETBIT(sc, 0xc0ac, 0x1f80);
    rge_write_mac_ocp(sc, 0xe098, 0xc302);
    RGE_MAC_CLRBIT(sc, 0xe032, 0x0003);
    v = rge_read_csi(sc, 0x98) & ~0x0000ff00;
    rge_write_csi(sc, 0x98, v);
    DELAY(20);
    RGE_MAC_CLRBIT(sc, 0xe092, 0x00ff);
    RGE_SETBIT_2(sc, RGE_CPLUSCMD, RGE_CPLUSCMD_RXCSUM);
    RGE_READ_2(sc, RGE_CPLUSCMD);
    RGE_WRITE_2(sc, RGE_RXMAXSIZE, sc->rge_type == MAC_R25B ? BUFSZ : (BUFSZ | RGE_RXCFG_VLANPHYS));
    RGE_CLRBIT_1(sc, RGE_PPSW, 0x08); /* RXDV gate off */
    v = RGE_READ_4(sc, RGE_RXCFG) & ~(RGE_RXCFG_ALLPHYS | RGE_RXCFG_MULTI);
    RGE_WRITE_4(sc, RGE_RXCFG, v | RGE_RXCFG_INDIV | RGE_RXCFG_BROAD | RGE_RXCFG_MULTI);
    RGE_WRITE_4(sc, RGE_MAR0, 0xffffffff);
    RGE_WRITE_4(sc, RGE_MAR4, 0xffffffff);
    rge_disable_aspm_clkreq(sc);
    RGE_CLRBIT_1(sc, RGE_EECMD, RGE_EECMD_WRITECFG);
    DELAY(10);
    /* Auto negotiation, like rge_ifmedia_upd */
    RGE_PHY_CLRBIT(sc, 0xa428, 0x0200);
    RGE_PHY_CLRBIT(sc, 0xa5ea, RGE_TYPE_R26(sc) ? 0x0007 : 0x0001);
    v = rge_read_phy_ocp(sc, 0xa5d4) & ~(RGE_ADV_2500TFDX | RGE_ADV_5000TFDX);
    v |= RGE_ADV_2500TFDX | (RGE_TYPE_R26(sc) ? RGE_ADV_5000TFDX : 0);
    rge_write_phy(sc, 0, MII_ANAR,
                  ANAR_TX_FD | ANAR_TX | ANAR_10_FD | ANAR_10 | ANAR_PAUSE_ASYM | ANAR_FC);
    rge_write_phy(sc, 0, MII_100T2CR, GTCR_ADV_1000TFDX | GTCR_ADV_1000THDX);
    rge_write_phy_ocp(sc, 0xa5d4, (u16)v);
    rge_write_phy(sc, 0, MII_BMCR, BMCR_RESET | BMCR_AUTOEN | BMCR_STARTNEG);
    RGE_WRITE_1(sc, RGE_CMD, RGE_CMD_TXENB | RGE_CMD_RXENB);
    RGE_WRITE_4(sc, RGE_IMR, 0); /* no interrupts, Violyx polls */
    stage = 6;
    return 0;
}

static u32 isrseen;
static u64 resync, kicks, lastrx;
static int dhtype, dhlen;
static u8 dhip[4];
static int macsrc;
static u8 mymac[6];
static u64 rxme, rxbc, rxdhcp, rxarp, *tally;
static int mac_ok(const u8 *m) {
    int z = 1, f = 1;
    for (int i = 0; i < 6; i++) {
        if (m[i])
            z = 0;
        if (m[i] != 0xFF)
            f = 0;
    }
    return !z && !f && !(m[0] & 1);
}
static void rx_count(const u8 *b, int n) {
    if (n < 14)
        return;
    if (!memcmp(b, mymac, 6))
        rxme++;
    if (b[0] == 0xFF)
        rxbc++;
    if (b[12] == 8 && b[13] == 6)
        rxarp++;
    if (n >= 42 && b[12] == 8 && b[13] == 0 && b[23] == 17 && b[36] == 0 && b[37] == 68) {
        rxdhcp++;
        dhlen = n;
        if (n > 282) {
            memcpy(dhip, b + 58, 4);
            for (int o = 282; o + 2 < n && b[o] != 255;) {
                if (b[o] == 0) {
                    o++;
                    continue;
                }
                if (b[o] == 53) {
                    dhtype = b[o + 2];
                    break;
                }
                o += 2 + b[o + 1];
            }
        }
    }
}
int rge_attach(const pci_t *c, u8 *mac) {
    struct rge_softc *sc = &S;
    stage = 1;
    pci_enable(c);
    u64 bar = pci_bar(c, 2);
    if (!bar)
        bar = pci_bar(c, 1);
    if (!bar)
        return -1;
    mmio_map(bar, 0x10000);
    sc->base = bar;
    hwrev = RGE_READ_4(sc, RGE_TXCFG) & RGE_TXCFG_HWREV;
    if (hwrev == 0x64100000)
        sc->rge_type = MAC_R25B;
    else if (hwrev == 0x64900000)
        sc->rge_type = MAC_R26_1;
    else if (hwrev == 0x64a00000)
        sc->rge_type = MAC_R26_2;
    else
        return -2;
    rl = dma_alloc(4096);
    tl = dma_alloc(4096);
    rb = dma_alloc(NRX * BUFSZ);
    tb = dma_alloc(NTX * BUFSZ);
    if (!rl || !tl || !rb || !tb)
        return -3;
    memset(rl, 0, 4096);
    memset(tl, 0, 4096);
    for (int i = 0; i < NRX; i++)
        rx_arm(i);
    tl[NTX - 1].rge_cmdsts = RGE_TDCMDSTS_EOR;
    /* Without firmware the backup register can be empty: then take the active address, finally an
     * own one */
    u8 m0[6];
    for (int i = 0; i < 6; i++)
        m0[i] = RGE_READ_1(sc, RGE_MAC0 + i);
    rge_get_macaddr(sc, mac);
    macsrc = 1;
    if (!mac_ok(mac)) {
        memcpy(mac, m0, 6);
        macsrc = 2;
    }
    if (!mac_ok(mac)) {
        static const u8 own[6] = {0x02, 0x56, 0x49, 0x4F, 0x4C, 0x59};
        memcpy(mac, own, 6);
        macsrc = 3;
    }
    memcpy(mymac, mac, 6);
    return chip_start(mac);
}

int rge_link(void) {
    u32 v = RGE_READ_2(&S, RGE_PHYSTAT);
    return v != 0xFFFF && (v & RGE_PHYSTAT_LINK) ? 1 : 0;
}

int rge_send(const void *b, int len) {
    if (len > BUFSZ)
        return -1;
    struct rge_tx_desc *d = &tl[txi];
    for (int i = 0; i < 200000 && (d->rge_cmdsts & RGE_TDCMDSTS_OWN); i++)
        udelay(1);
    if (d->rge_cmdsts & RGE_TDCMDSTS_OWN) {
        txbusy++;
        return -2;
    }
    memcpy(tb + txi * BUFSZ, b, len);
    if (len < 60) {
        memset(tb + txi * BUFSZ + len, 0, 60 - len);
        len = 60;
    } /* pad by hand */
    d->rge_addr = phys(tb + txi * BUFSZ);
    d->rge_extsts = 0;
    u32 c = RGE_TDCMDSTS_SOF | RGE_TDCMDSTS_EOF | len | (txi == NTX - 1 ? RGE_TDCMDSTS_EOR : 0);
    d->rge_cmdsts = c;
    __asm__ volatile("mfence" : : : "memory");
    d->rge_cmdsts = c | RGE_TDCMDSTS_OWN;
    __asm__ volatile("mfence" : : : "memory");
    txi = (txi + 1) % NTX;
    RGE_WRITE_2(&S, RGE_TXSTART, RGE_TXSTART_START);
    txok++;
    return 0;
}

/* Returns the length of the next frame (0 = none). Call rge_recv_done afterwards. */
static int gone(void) {
    return RGE_READ_1(&S, RGE_CMD) == 0xFF;
}
int rge_recv(void **out) {
    /* Without interrupts nobody else acknowledges the status. A pending "no receive slots" stops
     * the chip. */
    u32 isr = RGE_READ_4(&S, RGE_ISR);
    if (isr && isr != 0xFFFFFFFF) {
        RGE_WRITE_4(&S, RGE_ISR, isr);
        isrseen |= isr;
    }
    for (int n = 0; n < NRX; n++) {
        u32 st = rl[rxi].hi_qword1.rx_qword4.rge_cmdsts;
        if (st & RGE_RDCMDSTS_OWN) {
            /* Chip and driver out of step? Then continue at the next full slot. */
            int found = -1;
            for (int k = 1; k < NRX; k++) {
                int q = (rxi + k) % NRX;
                if (!(rl[q].hi_qword1.rx_qword4.rge_cmdsts & RGE_RDCMDSTS_OWN)) {
                    found = q;
                    break;
                }
            }
            if (found >= 0) {
                resync++;
                rxi = found;
                continue;
            }
            /* The ring is free but the chip reports "no slots": restart receiving */
            if ((isr & RGE_ISR_RX_DESC_UNAVAIL) && !gone()) {
                kicks++;
                RGE_WRITE_1(&S, RGE_CMD, RGE_CMD_TXENB);
                udelay(50);
                RGE_WRITE_4(&S, RGE_RXDESC_ADDR_LO, RGE_ADDR_LO(phys(rl)));
                RGE_WRITE_4(&S, RGE_RXDESC_ADDR_HI, RGE_ADDR_HI(phys(rl)));
                rxi = 0;
                RGE_WRITE_1(&S, RGE_CMD, RGE_CMD_TXENB | RGE_CMD_RXENB);
            }
            return 0;
        }
        int len = (int)(mask_rd(st) & RGE_RDCMDSTS_FRAGLEN);
        if ((st & RGE_RDCMDSTS_RXERRSUM) || !(st & RGE_RDCMDSTS_SOF) || !(st & RGE_RDCMDSTS_EOF) ||
            len <= 4 || len > BUFSZ) {
            rxerr++;
            rx_arm(rxi);
            rxi = (rxi + 1) % NRX;
            continue;
        }
        rxok++;
        rx_count(rb + rxi * BUFSZ, len);
        *out = rb + rxi * BUFSZ;
        return len - 4; /* without CRC */
    }
    return 0;
}
void rge_recv_done(void) {
    rx_arm(rxi);
    rxi = (rxi + 1) % NRX;
}

void rge_diag(void (*cb)(const char *, void *), void *a) {
    char l[72];
    struct rge_softc *sc = &S;
    if (!stage) {
        cb("Realtek: not found", a);
        return;
    }
    ksnprintf(l, 72, "Realtek hwrev %x stage %d/6", hwrev, stage);
    cb(l, a);
    if (!sc->base)
        return;
    u32 st = RGE_READ_2(sc, RGE_PHYSTAT);
    ksnprintf(l, 72, "phystat %x link %d phy %x rcode %x", st, (st & RGE_PHYSTAT_LINK) ? 1 : 0,
              rge_read_phy_ocp(sc, 0xa420), sc->rge_rcodever);
    cb(l, a);
    ksnprintf(l, 72, "cmd %x rxcfg %x txcfg %x", RGE_READ_1(sc, RGE_CMD), RGE_READ_4(sc, RGE_RXCFG),
              RGE_READ_4(sc, RGE_TXCFG));
    cb(l, a);
    ksnprintf(l, 72, "tx %d rx %d err %d busy %d txi %d rxi %d", (int)txok, (int)rxok, (int)rxerr,
              (int)txbusy, txi, rxi);
    cb(l, a);
    ksnprintf(l, 72, "mac %02x:%02x:%02x:%02x:%02x:%02x src %d", mymac[0], mymac[1], mymac[2],
              mymac[3], mymac[4], mymac[5], macsrc);
    cb(l, a);
    ksnprintf(l, 72, "rx me %d bcast %d arp %d dhcp %d", (int)rxme, (int)rxbc, (int)rxarp,
              (int)rxdhcp);
    cb(l, a);
    int last = (txi + NTX - 1) % NTX;
    ksnprintf(l, 72, "isr %x seen %x txd0 %x last %x", RGE_READ_4(sc, RGE_ISR), isrseen,
              tl[0].rge_cmdsts, tl[last].rge_cmdsts);
    cb(l, a);
    ksnprintf(l, 72, "ring resync %d kick %d dhcp type %d len %d ip %d.%d.%d.%d", (int)resync,
              (int)kicks, dhtype, dhlen, dhip[0], dhip[1], dhip[2], dhip[3]);
    cb(l, a);
    /* Chip counter: what really went out on the wire */
    if (!tally)
        tally = dma_alloc(4096);
    u32 lo = 0;
    if (tally) {
        u64 pa = phys(tally);
        RGE_WRITE_4(sc, 0x14, RGE_ADDR_HI(pa));
        RGE_READ_1(sc, RGE_CMD);
        RGE_WRITE_4(sc, 0x10, RGE_ADDR_LO(pa));
        RGE_WRITE_4(sc, 0x10, RGE_ADDR_LO(pa) | 0x08);
        for (int i = 0; i < 1000; i++) {
            lo = RGE_READ_4(sc, 0x10);
            if (!(lo & 0x08))
                break;
            udelay(10);
        }
        ksnprintf(l, 72, "chip tx %d rx %d txerr %d rxerr %d miss %d %s", (int)tally[0],
                  (int)tally[1], (int)tally[2], (int)(u32)tally[3], (int)(tally[3] >> 32 & 0xFFFF),
                  (lo & 0x08) ? "timeout" : "ok");
        cb(l, a);
    }
}
