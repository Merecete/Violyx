#include "os.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/platform_time.h"
#include "psa/crypto.h"
#include "ca_der.h"
/* TLS-Client auf Mbed TLS. Zertifikate werden immer geprueft: Kette, Name, Datum. */
void *k_calloc(size_t n,size_t s){ if(n&&s>(1ull<<31)/n)return 0; void*p=kmalloc(n*s); if(p)memset(p,0,n*s); return p; }
int k_noprintf(const char*f,...){ return 0; } int k_nofprintf(void*a,const char*f,...){ return 0; } void k_exit(int c){ panic("Mbed TLS: exit"); }
long long k_time(long long*t){ rtc_t r; rtc_read(&r); long long v=rtc_unix(&r); if(t)*t=v; return v; }
mbedtls_ms_time_t mbedtls_ms_time(void){ return (mbedtls_ms_time_t)ticks*10; }
struct tm *mbedtls_platform_gmtime_r(const mbedtls_time_t*tt,struct tm*o){ rtc_t r; rtc_from_unix((u32)*tt,&r); memset(o,0,sizeof*o); o->tm_sec=r.s;o->tm_min=r.m;o->tm_hour=r.h;o->tm_mday=r.day;o->tm_mon=r.mon-1;o->tm_year=r.year-1900; return o; }
static int weak;
static int cpu_ok(int leaf,int reg,int bit){ u32 a=leaf,b,c=0,d; __asm__ volatile("cpuid":"+a"(a),"=b"(b),"+c"(c),"=d"(d)); u32 v=reg==1?b:c; return v>>bit&1; }
int mbedtls_hardware_poll(void*x,unsigned char*o,size_t n,size_t*ol){ int sd=cpu_ok(7,1,18),rd=cpu_ok(1,2,30); if(!sd&&!rd)weak=1; size_t i=0;
  while(i<n){ u64 v=0; unsigned char ok=0; for(int t=0;t<64&&!ok;t++){ if(sd) __asm__ volatile("rdseed %0; setc %1":"=r"(v),"=q"(ok)); else if(rd) __asm__ volatile("rdrand %0; setc %1":"=r"(v),"=q"(ok)); else break; }
    u32 a,d; for(int k=0;k<16;k++){ __asm__ volatile("rdtsc":"=a"(a),"=d"(d)); v=(v<<7|v>>57)^a^((u64)d<<32); udelay(1); } for(int k=0;k<8&&i<n;k++){ o[i++]=v; v>>=8; } } *ol=n; return 0; }
static mbedtls_ssl_context ssl; static mbedtls_ssl_config cf; static mbedtls_x509_crt ca; static mbedtls_entropy_context en; static mbedtls_ctr_drbg_context dr; static int ready,live;
char tls_err[80];
int tls_weak(void){ return weak; }
static int b_send(void*c,const unsigned char*b,size_t n){ int r=tcp_tx(b,n); return r<0?MBEDTLS_ERR_SSL_INTERNAL_ERROR:r; }
static int b_recv(void*c,unsigned char*b,size_t n){ int r=tcp_rx(b,n); return r<0?MBEDTLS_ERR_SSL_CONN_EOF:r; }
static int setup(void){ if(ready)return 0; mbedtls_ssl_config_init(&cf); mbedtls_x509_crt_init(&ca); mbedtls_entropy_init(&en); mbedtls_ctr_drbg_init(&dr);
  if(psa_crypto_init()!=PSA_SUCCESS)return -1; if(mbedtls_ctr_drbg_seed(&dr,mbedtls_entropy_func,&en,(const unsigned char*)"violyx",6))return -1;
  int good=0; for(unsigned i=0;i<CA_COUNT;i++) if(!mbedtls_x509_crt_parse_der_nocopy(&ca,ca_der+ca_off[i],ca_len[i]))good++; if(!good)return -1;
  if(mbedtls_ssl_config_defaults(&cf,MBEDTLS_SSL_IS_CLIENT,MBEDTLS_SSL_TRANSPORT_STREAM,MBEDTLS_SSL_PRESET_DEFAULT))return -1;
  mbedtls_ssl_conf_authmode(&cf,MBEDTLS_SSL_VERIFY_REQUIRED); mbedtls_ssl_conf_ca_chain(&cf,&ca,0); mbedtls_ssl_conf_rng(&cf,mbedtls_ctr_drbg_random,&dr); mbedtls_ssl_conf_min_tls_version(&cf,MBEDTLS_SSL_VERSION_TLS1_2); ready=1; return 0; }
int tls_open(const char*host){ tls_err[0]=0; if(setup()){ strlcpy_(tls_err,TR("TLS-Einrichtung fehlgeschlagen","TLS setup failed"),80); return -1; } mbedtls_ssl_init(&ssl); live=1; if(mbedtls_ssl_setup(&ssl,&cf)||mbedtls_ssl_set_hostname(&ssl,host)){ strlcpy_(tls_err,TR("TLS-Einrichtung fehlgeschlagen","TLS setup failed"),80); return -1; } mbedtls_ssl_set_bio(&ssl,0,b_send,b_recv,0);
  for(;;){ int r=mbedtls_ssl_handshake(&ssl); if(!r)break; if(r==MBEDTLS_ERR_SSL_WANT_READ||r==MBEDTLS_ERR_SSL_WANT_WRITE)continue;
    u32 f=mbedtls_ssl_get_verify_result(&ssl); if(r==MBEDTLS_ERR_X509_CERT_VERIFY_FAILED||(f&&f!=0xFFFFFFFFu)){ const char*w=(f&MBEDTLS_X509_BADCERT_EXPIRED)?TR("abgelaufen","expired"):(f&MBEDTLS_X509_BADCERT_FUTURE)?TR("noch nicht gültig (Uhrzeit prüfen)","not valid yet (check the clock)"):(f&MBEDTLS_X509_BADCERT_CN_MISMATCH)?TR("falscher Name","wrong name"):(f&MBEDTLS_X509_BADCERT_NOT_TRUSTED)?TR("unbekannter Aussteller","unknown issuer"):TR("ungültig","invalid"); ksnprintf(tls_err,80,TR("Zertifikat abgelehnt: %s","Certificate rejected: %s"),w); return -2; }
    ksnprintf(tls_err,80,TR("TLS-Fehler -0x%x","TLS error -0x%x"),(u32)-r); return -1; } return 0; }
int tls_write(const void*b,int n){ int o=0; while(o<n){ int r=mbedtls_ssl_write(&ssl,(const u8*)b+o,n-o); if(r==MBEDTLS_ERR_SSL_WANT_READ||r==MBEDTLS_ERR_SSL_WANT_WRITE)continue; if(r<0)return -1; o+=r; } return o; }
int tls_read(void*b,int n){ for(;;){ int r=mbedtls_ssl_read(&ssl,b,n); if(r==MBEDTLS_ERR_SSL_WANT_READ||r==MBEDTLS_ERR_SSL_WANT_WRITE||r==MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET)continue; if(r==MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)return 0; if(r<0)return r==MBEDTLS_ERR_SSL_CONN_EOF?0:-1; return r; } }
void tls_close(void){ if(live){ mbedtls_ssl_close_notify(&ssl); mbedtls_ssl_free(&ssl); live=0; } }
