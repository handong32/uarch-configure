#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <inttypes.h>
#include <unistd.h>
#include <math.h>
#include <string.h>

/* Intel support */

#define MSR_INTEL_RAPL_POWER_UNIT		0x606
/*
 * Platform specific RAPL Domains.
 * Note that PP1 RAPL Domain is supported on 062A only
 * And DRAM RAPL Domain is supported on 062D only
 */
/* Package RAPL Domain */
#define MSR_PKG_RAPL_POWER_LIMIT	0x610
#define MSR_INTEL_PKG_ENERGY_STATUS	0x611
#define MSR_PKG_PERF_STATUS		0x613
#define MSR_PKG_POWER_INFO		0x614

/* PP0 RAPL Domain */
#define MSR_PP0_POWER_LIMIT		0x638
#define MSR_INTEL_PP0_ENERGY_STATUS	0x639
#define MSR_PP0_POLICY			0x63A
#define MSR_PP0_PERF_STATUS		0x63B

/* PP1 RAPL Domain, may reflect to uncore devices */
#define MSR_PP1_POWER_LIMIT		0x640
#define MSR_PP1_ENERGY_STATUS		0x641
#define MSR_PP1_POLICY			0x642

/* DRAM RAPL Domain */
#define MSR_DRAM_POWER_LIMIT		0x618
#define MSR_DRAM_ENERGY_STATUS		0x619
#define MSR_DRAM_PERF_STATUS		0x61B
#define MSR_DRAM_POWER_INFO		0x61C

/* PSYS RAPL Domain */
#define MSR_PLATFORM_ENERGY_STATUS	0x64d

/* RAPL UNIT BITMASK */
#define POWER_UNIT_OFFSET	0
#define POWER_UNIT_MASK		0x0F

#define ENERGY_UNIT_OFFSET	0x08
#define ENERGY_UNIT_MASK	0x1F00

#define TIME_UNIT_OFFSET	0x10
#define TIME_UNIT_MASK		0xF000

#define CPU_VENDOR_INTEL	1

#define CPU_SANDYBRIDGE		42
#define CPU_SANDYBRIDGE_EP	45
#define CPU_IVYBRIDGE		58
#define CPU_IVYBRIDGE_EP	62
#define CPU_HASWELL		60
#define CPU_HASWELL_ULT		69
#define CPU_HASWELL_GT3E	70
#define CPU_HASWELL_EP		63
#define CPU_BROADWELL		61
#define CPU_BROADWELL_GT3E	71
#define CPU_BROADWELL_EP	79
#define CPU_BROADWELL_DE	86
#define CPU_SKYLAKE		78
#define CPU_SKYLAKE_HS		94
#define CPU_SKYLAKE_X		85
#define CPU_KNIGHTS_LANDING	87
#define CPU_KNIGHTS_MILL	133
#define CPU_KABYLAKE_MOBILE	142
#define CPU_KABYLAKE		158
#define CPU_ATOM_SILVERMONT	55
#define CPU_ATOM_AIRMONT	76
#define CPU_ATOM_MERRIFIELD	74
#define CPU_ATOM_MOOREFIELD	90
#define CPU_ATOM_GOLDMONT	92
#define CPU_ATOM_GEMINI_LAKE	122
#define CPU_ATOM_DENVERTON	95

#define RAPL_MAX_CPUS	1024
#define RAPL_MAX_PACKAGES	16

int rapl_total_packages=0, rapl_cpu_model, rapl_package_map[RAPL_MAX_PACKAGES];
unsigned int msr_rapl_units,msr_pkg_energy_status,msr_pp0_energy_status;
int rapl_dram_avail=0,rapl_pp0_avail=0;
double rapl_power_units,rapl_time_units;
double rapl_cpu_energy_units[RAPL_MAX_PACKAGES],rapl_dram_energy_units[RAPL_MAX_PACKAGES];
double rapl_package_before[RAPL_MAX_PACKAGES],rapl_package_after[RAPL_MAX_PACKAGES];
double rapl_pp0_before[RAPL_MAX_PACKAGES],rapl_pp0_after[RAPL_MAX_PACKAGES];
double rapl_dram_before[RAPL_MAX_PACKAGES],rapl_dram_after[RAPL_MAX_PACKAGES];

int rapl_open_msr(int core) {

	char msr_filename[BUFSIZ];
	int fd;

	sprintf(msr_filename, "/dev/cpu/%d/msr", core);
	fd = open(msr_filename, O_RDWR);
	if ( fd < 0 ) {
		if ( errno == ENXIO ) {
			fprintf(stderr, "rdmsr: No CPU %d\n", core);
			exit(2);
		} else if ( errno == EIO ) {
			fprintf(stderr, "rdmsr: CPU %d doesn't support MSRs\n",
					core);
			exit(3);
		} else {
			perror("rdmsr:open");
			fprintf(stderr,"Trying to open %s\n",msr_filename);
			exit(127);
		}
	}

	return fd;
}

long long rapl_read_msr(int fd, unsigned int which) {

	uint64_t data;

	if ( pread(fd, &data, sizeof data, which) != sizeof data ) {
		perror("rdmsr:pread");
		fprintf(stderr,"Error reading MSR %x\n",which);
		exit(127);
	}

	return (long long)data;
}

void RAPL_INIT() {
  // detect CPU
  FILE *fff;

  int vendor=-1,family,model=-1,package,i;
  char buffer[BUFSIZ],*result;
  char vendor_string[BUFSIZ];
  char filename[BUFSIZ];
  int fd,j;
  long long res;
  
  fff=fopen("/proc/cpuinfo", "r");
  if (fff==NULL) exit(-1);;

  while(1) {
    result=fgets(buffer,BUFSIZ,fff);
    if (result==NULL) break;

    if (!strncmp(result,"vendor_id",8)) {
      sscanf(result,"%*s%*s%s",vendor_string);

      if (!strncmp(vendor_string,"GenuineIntel",12)) {
	vendor=CPU_VENDOR_INTEL;
      }
    }

    if (!strncmp(result,"cpu family",10)) {
      sscanf(result,"%*s%*s%*s%d",&family);
    }

    if (!strncmp(result,"model",5)) {
      sscanf(result,"%*s%*s%d",&model);
    }
  }

  if (vendor==CPU_VENDOR_INTEL) {
    if (family!=6) {
      printf("Wrong CPU family %d\n",family);
      exit(-1);
    }

    msr_rapl_units=MSR_INTEL_RAPL_POWER_UNIT;
    msr_pkg_energy_status=MSR_INTEL_PKG_ENERGY_STATUS;
    msr_pp0_energy_status=MSR_INTEL_PP0_ENERGY_STATUS;

    printf("Found ");

    switch(model) {
    case CPU_SANDYBRIDGE:
      printf("Sandybridge");
      break;
    case CPU_SANDYBRIDGE_EP:
      printf("Sandybridge-EP");
      break;
    case CPU_IVYBRIDGE:
      printf("Ivybridge");
      break;
    case CPU_IVYBRIDGE_EP:
      printf("Ivybridge-EP");
      break;
    case CPU_HASWELL:
    case CPU_HASWELL_ULT:
    case CPU_HASWELL_GT3E:
      printf("Haswell");
      break;
    case CPU_HASWELL_EP:
      printf("Haswell-EP");
      break;
    case CPU_BROADWELL:
    case CPU_BROADWELL_GT3E:
      printf("Broadwell");
      break;
    case CPU_BROADWELL_EP:
      printf("Broadwell-EP");
      break;
    case CPU_SKYLAKE:
    case CPU_SKYLAKE_HS:
      printf("Skylake");
      break;
    case CPU_SKYLAKE_X:
      printf("Skylake-X");
      break;
    case CPU_KABYLAKE:
    case CPU_KABYLAKE_MOBILE:
      printf("Kaby Lake");
      break;
    case CPU_KNIGHTS_LANDING:
      printf("Knight's Landing");
      break;
    case CPU_KNIGHTS_MILL:
      printf("Knight's Mill");
      break;
    case CPU_ATOM_GOLDMONT:
    case CPU_ATOM_GEMINI_LAKE:
    case CPU_ATOM_DENVERTON:
      printf("Atom");
      break;
    default:
      printf("Unsupported model %d\n",model);
      model=-1;
      break;
    }
  }

  fclose(fff);

  printf(" Processor type\n");
  rapl_cpu_model = model;

  if (rapl_cpu_model<0) {
    printf("\tUnsupported CPU model %d\n",rapl_cpu_model);
    exit(-1);
  }
	
  //detect packages
  for(i=0;i<RAPL_MAX_PACKAGES;i++) rapl_package_map[i]=-1;
  
  printf("\t");
  for(i=0;i<RAPL_MAX_CPUS;i++) {
    sprintf(filename,"/sys/devices/system/cpu/cpu%d/topology/physical_package_id",i);
    fff=fopen(filename,"r");
    if (fff==NULL) break;
    fscanf(fff,"%d",&package);
    printf("%d (%d)",i,package);
    if (i%8==7) printf("\n\t"); else printf(", ");
    fclose(fff);

    if (rapl_package_map[package]==-1) {
      rapl_total_packages++;
      rapl_package_map[package]=i;
    }
  }

  printf("\n");
  printf("\tDetected %d packages\n\n", rapl_total_packages);

  switch(rapl_cpu_model) {
  case CPU_SANDYBRIDGE_EP:
  case CPU_IVYBRIDGE_EP:
    rapl_pp0_avail=1;
    rapl_dram_avail=1;
    break;
  default:
    printf("\t Unsupported CPU type\n");
    exit(-1);
    break;
  }

  for(j=0;j<rapl_total_packages;j++) {
    printf("\tListing paramaters for package #%d\n",j);
    
    fd=rapl_open_msr(rapl_package_map[j]);
    
    /* Calculate the units used */
    res=rapl_read_msr(fd, msr_rapl_units);

    rapl_power_units=pow(0.5,(double)(res&0xf));
    rapl_cpu_energy_units[j]=pow(0.5,(double)((res>>8)&0x1f));
    rapl_time_units=pow(0.5,(double)((res>>16)&0xf));
    rapl_dram_energy_units[j]=rapl_cpu_energy_units[j];

    printf("\t\tPower units = %.3fW\n",rapl_power_units);
    printf("\t\tCPU Energy units = %.8fJ\n",rapl_cpu_energy_units[j]);
    printf("\t\tDRAM Energy units = %.8fJ\n",rapl_dram_energy_units[j]);
    printf("\t\tTime units = %.8fs\n",rapl_time_units);
    printf("\n");
    close(fd);
  }
  printf("\n");
}

void RAPL_START() {
  int j, fd;
  long long result;
  
  for(j=0;j<rapl_total_packages;j++) {

    fd=rapl_open_msr(rapl_package_map[j]);

    /* Package Energy */
    result=rapl_read_msr(fd,msr_pkg_energy_status);
    rapl_package_before[j]=(double)result*rapl_cpu_energy_units[j];

    /* PP0 energy */
    /* Not available on Knights* */
    // Always returns zero on Haswell-EP?
    if (rapl_pp0_avail) {
      result=rapl_read_msr(fd,msr_pp0_energy_status);
      rapl_pp0_before[j]=(double)result*rapl_cpu_energy_units[j];
    }

    /* Updated documentation (but not the Vol3B) says Haswell and	*/
    /* Broadwell have DRAM support too				*/
    if (rapl_dram_avail) {
      result=rapl_read_msr(fd,MSR_DRAM_ENERGY_STATUS);
      rapl_dram_before[j]=(double)result*rapl_dram_energy_units[j];
    }
    close(fd);
  }
}

void RAPL_STOP() {
  int j, fd;
  long long result;
  
  for(j=0;j<rapl_total_packages;j++) {	  
    fd=rapl_open_msr(rapl_package_map[j]);

    printf("\tPackage %d:\n",j);
		
    result=rapl_read_msr(fd,msr_pkg_energy_status);
    rapl_package_after[j]=(double)result*rapl_cpu_energy_units[j];
    printf("\t\tPackage energy: %.6fJ\n",
	   rapl_package_after[j]-rapl_package_before[j]);

    result=rapl_read_msr(fd,msr_pp0_energy_status);
    rapl_pp0_after[j]=(double)result*rapl_cpu_energy_units[j];
    printf("\t\tPowerPlane0 (cores): %.6fJ\n",
	   rapl_pp0_after[j]-rapl_pp0_before[j]);		
    
    result=rapl_read_msr(fd,MSR_DRAM_ENERGY_STATUS);
    rapl_dram_after[j]=(double)result*rapl_dram_energy_units[j];
    printf("\t\tDRAM: %.6fJ\n",
	   rapl_dram_after[j]-rapl_dram_before[j]);
    close(fd);
  }
  printf("\n");
}

void RAPL_POWER_MOD(int j, unsigned int npower) {
  int fd;
  uint64_t result = 0;
  uint64_t m = 0x7FFF;
  
  fd=rapl_open_msr(rapl_package_map[j]);
  
  result=rapl_read_msr(fd,MSR_PKG_RAPL_POWER_LIMIT);
			  
  // resetting values
  result = result & (~m);
  result = result & (~(m << 32));

  // new power
  result = result | npower;
  result = result | ((uint64_t)npower << 32);

  // set clamp
  result |= 1LL << 15;
  result |= 1LL << 16;
  result |= 1LL << 47;
  result |= 1LL << 48;
  
  if ( pwrite(fd, &result, sizeof(result), MSR_PKG_RAPL_POWER_LIMIT) != sizeof(result) ) {
    perror("rdmsr:write");
    fprintf(stderr,"Error writing MSR %x\n", MSR_PKG_RAPL_POWER_LIMIT);
    exit(127);
  }

  /* Show package power limit */
  result=rapl_read_msr(fd,MSR_PKG_RAPL_POWER_LIMIT);		  
  printf("Package power limits are %s\n", (result >> 63) ? "locked" : "unlocked");
  double pkg_power_limit_1 = rapl_power_units*(double)((result>>0)&0x7FFF);
  double pkg_time_window_1 = rapl_time_units*(double)((result>>17)&0x007F);
  printf("Package power limit #1: %.3fW for %.6fs (%s, %s)\n",
	 pkg_power_limit_1, pkg_time_window_1,
	 (result & (1LL<<15)) ? "enable power limit" : "disabled",
	 (result & (1LL<<16)) ? "clamped" : "not_clamped");
  double pkg_power_limit_2 = rapl_power_units*(double)((result>>32)&0x7FFF);
  double pkg_time_window_2 = rapl_time_units*(double)((result>>49)&0x007F);
  printf("Package power limit #2: %.3fW for %.6fs (%s, %s)\n", 
	 pkg_power_limit_2, pkg_time_window_2,
	 (result & (1LL<<47)) ? "enable power limit" : "disabled",
	 (result & (1LL<<48)) ? "clamped" : "not_clamped");
  
  close(fd);
}

int main(int argc, char**argv)
{
  if(argc != 2) {
    printf("argument error\n");
    return 0;
  }

  int power = atoi(argv[1]);
  if (power < 10 || power > 215) {
    printf("Power spec 10 - 215 Watts\n");
    return 0;
  }
  
  unsigned int npower = (unsigned int)(power/0.125);
  
  RAPL_INIT();
  RAPL_POWER_MOD(0, npower);
  RAPL_POWER_MOD(1, npower);
  
  return 0;
}
