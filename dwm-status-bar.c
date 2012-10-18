/* DWM Status Bar
 *
 * Compile with:
 * gcc -Wall -lX11 -o dwm-status-bar dwm-status-bar.c
 */

/* Standard libraries */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* X11 */
#include <X11/Xlib.h>

/* Date / time */
#include <time.h>

/* To check network IP address */
#include <net/if.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>

/* To read /var/run/network/profiles directory */
#include <dirent.h>

static Display *dpy;

/* Colour escape sequences */
char Title[]  = "\x01";
char Unit[]   = "\x04";
char Value[]  = "\x02";
char Clock[]  = "\x05";
char Green[]  = "\x06";
char Amber[]  = "\x07";
char Red[]    = "\x08";

char Sep[]    = "\x01|\x02";
char Degree[] = "\u00B0";


void get_colour(char **colour, int number, int threshold_amber, int threshold_red) {
   *colour = Value;
   if (number >= threshold_red)
      *colour = Red;
   else if (number >= threshold_amber)
      *colour = Amber;
}

float read_file_int(char *file) {
   FILE *fd;
   int ret;

   if(!(fd = fopen(file, "r"))) {
      fprintf(stderr, "Cannot open '%s' for reading.\n", file);
      exit(1);
   }

   fscanf(fd, "%d", &ret);
   fclose(fd);

   return ret;
}

void get_cpu_freqs(int *cpu0, int *cpu1, int *cpu2, int *cpu3) {
   *cpu0 = (float)read_file_int("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq")/1000;
   *cpu1 = (float)read_file_int("/sys/devices/system/cpu/cpu1/cpufreq/scaling_cur_freq")/1000;
   *cpu2 = (float)read_file_int("/sys/devices/system/cpu/cpu2/cpufreq/scaling_cur_freq")/1000;
   *cpu3 = (float)read_file_int("/sys/devices/system/cpu/cpu3/cpufreq/scaling_cur_freq")/1000;
}

void get_proc_stat_row(FILE *fd, int *idle_jiffies, int *total_jiffies) {
   int user, nice, system, idle, iowait,irq, softirq, steal, quest, guest_nice;

   fscanf(fd, "%*s %d %d %d %d %d %d %d %d %d %d\n", &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal, &quest, &guest_nice);
   *idle_jiffies = idle + iowait;
   *total_jiffies = user + nice + system + idle + iowait + irq + softirq + steal + quest + guest_nice;
}

void get_cpu_loads(int *cpu0, int *cpu1, int *cpu2, int *cpu3) {

   static int cpu0_prev_idle, cpu0_prev_total;
   static int cpu1_prev_idle, cpu1_prev_total;
   static int cpu2_prev_idle, cpu2_prev_total;
   static int cpu3_prev_idle, cpu3_prev_total;

   int tmp_idle, tmp_total;
   int cpu0_idle, cpu0_total;
   int cpu1_idle, cpu1_total;
   int cpu2_idle, cpu2_total;
   int cpu3_idle, cpu3_total;

   FILE *fd;

   if(!(fd = fopen("/proc/stat", "r"))) {
      fprintf(stderr, "Cannot open '/proc/stat' for reading.\n");
      exit(1);
   }

   get_proc_stat_row(fd, &tmp_idle, &tmp_total);
   get_proc_stat_row(fd, &cpu0_idle, &cpu0_total);
   get_proc_stat_row(fd, &cpu1_idle, &cpu1_total);
   get_proc_stat_row(fd, &cpu2_idle, &cpu2_total);
   get_proc_stat_row(fd, &cpu3_idle, &cpu3_total);

   get_proc_stat_row(fd, &tmp_idle, &tmp_total);
   cpu0_idle += tmp_idle; cpu0_total += tmp_total;

   get_proc_stat_row(fd, &tmp_idle, &tmp_total);
   cpu1_idle += tmp_idle; cpu1_total += tmp_total;

   get_proc_stat_row(fd, &tmp_idle, &tmp_total);
   cpu2_idle += tmp_idle; cpu2_total += tmp_total;

   get_proc_stat_row(fd, &tmp_idle, &tmp_total);
   cpu3_idle += tmp_idle; cpu3_total += tmp_total;

   *cpu0 = (float)((cpu0_total-cpu0_prev_total) - (cpu0_idle-cpu0_prev_idle)) / (cpu0_total-cpu0_prev_total) * 100 + 0.5;
   *cpu1 = (float)((cpu1_total-cpu1_prev_total) - (cpu1_idle-cpu1_prev_idle)) / (cpu1_total-cpu1_prev_total) * 100 + 0.5;
   *cpu2 = (float)((cpu2_total-cpu2_prev_total) - (cpu2_idle-cpu2_prev_idle)) / (cpu2_total-cpu2_prev_total) * 100 + 0.5;
   *cpu3 = (float)((cpu3_total-cpu3_prev_total) - (cpu3_idle-cpu3_prev_idle)) / (cpu3_total-cpu3_prev_total) * 100 + 0.5;

   cpu0_prev_idle = cpu0_idle; cpu0_prev_total = cpu0_total;
   cpu1_prev_idle = cpu1_idle; cpu1_prev_total = cpu1_total;
   cpu2_prev_idle = cpu2_idle; cpu2_prev_total = cpu2_total;
   cpu3_prev_idle = cpu3_idle; cpu3_prev_total = cpu3_total;

   fclose(fd);
}

void get_cpu_temps(int *temp2, int *temp3, int *temp4, int *temp5) {
   *temp2 = (float)read_file_int("/sys/devices/platform/coretemp.0/temp2_input")/1000;
   *temp3 = (float)read_file_int("/sys/devices/platform/coretemp.0/temp3_input")/1000;
   *temp4 = (float)read_file_int("/sys/devices/platform/coretemp.0/temp4_input")/1000;
   *temp5 = (float)read_file_int("/sys/devices/platform/coretemp.0/temp5_input")/1000;
}

void format_core_str(char *cpuN, int freq, int load, int temp) {
   char *load_colour, *temp_colour;

   get_colour(&load_colour, load, 75, 90);
   get_colour(&temp_colour, temp, 80, 90);

   snprintf(cpuN, 24, "%s%4d%sMHz %s%2d%s%% %s%2d%s%sC", Value, freq, Unit, load_colour, load, Unit, temp_colour, temp, Unit, Degree);
}

void get_cpu_str(char *cpu) {
   int freq0, freq1, freq2, freq3;
   int load0, load1, load2, load3;
   int temp0, temp1, temp2, temp3;
   char cpu0[24], cpu1[24], cpu2[24], cpu3[24];

   get_cpu_freqs(&freq0, &freq1, &freq2, &freq3);
   get_cpu_loads(&load0, &load1, &load2, &load3);
   get_cpu_temps(&temp0, &temp1, &temp2, &temp3);

   format_core_str(cpu0, freq0, load0, temp0);
   format_core_str(cpu1, freq1, load1, temp1);
   format_core_str(cpu2, freq2, load2, temp2);
   format_core_str(cpu3, freq3, load3, temp3);

   snprintf(cpu, 109, "%s %s%s %s%s %s%s%s", cpu0, Sep, cpu1, Sep, cpu2, Sep, cpu3, Title);
}

void get_fan_str (char *fan) {
   int fan1, fan2;
   char *fan1_colour, *fan2_colour;

   fan1 = read_file_int("/sys/devices/platform/applesmc.768/fan1_input");
   fan2 = read_file_int("/sys/devices/platform/applesmc.768/fan2_input");

   get_colour(&fan1_colour, fan1, 4500, 5800);
   get_colour(&fan2_colour, fan2, 4500, 5800);

   snprintf(fan, 25, "%s%4d%sRPM %s %s%4d%sRPM%s", fan1_colour, fan1, Unit, Sep, fan2_colour, fan2, Unit, Title);
}

void get_mem_str(char *mem) {
   FILE *fd;
   int total, free, buffers, cached;
   int total_used;

   if(!(fd = fopen("/proc/meminfo", "r"))) {
      fprintf(stderr, "Cannot open '/proc/meminfo' for reading.\n");
      return;
   }
   fscanf(fd, "MemTotal: %d kB\n", &total);
   fscanf(fd, "MemFree: %d kB\n", &free);
   fscanf(fd, "Buffers: %d kB\n", &buffers);
   fscanf(fd, "Cached: %d kB\n", &cached);
   total_used = (total - free - buffers - cached) /1024;
   fclose(fd);
   snprintf(mem, 11, "%s%d%sMB%s", Value, total_used, Unit, Title);
}

void get_interface_stats(char *interface, int *carrier, int *rx, int *tx) {
   char filename[41];

   snprintf(filename, 41, "/sys/class/net/%s/carrier", interface);
   *carrier = read_file_int(filename);

   snprintf(filename, 41, "/sys/class/net/%s/statistics/rx_bytes", interface);
   *rx = (float)read_file_int(filename)/1024/1024;

   snprintf(filename, 41, "/sys/class/net/%s/statistics/tx_bytes", interface);
   *tx = (float)read_file_int(filename)/1024/1024;
}

int check_interface_ip(char *interface) {
   int s, ret;
   struct ifreq ifr[1];

   s = socket(AF_INET, SOCK_DGRAM, 0);
   snprintf(ifr[0].ifr_name, IFNAMSIZ, interface);
   ret = ioctl(s, SIOCGIFADDR, &ifr);
   close(s);
   return ret;
}

void get_interface_colour(char **colour, char *interface, int carrier) {
   if (carrier == 1 )
      if (check_interface_ip(interface) == 0)
         *colour = Green;
      else
         *colour = Amber;
   else
      *colour = Red;
}

void get_net_str(char *net) {
   int eth_carrier, eth_rx, eth_tx;
   int wlan_carrier, wlan_rx, wlan_tx;
   char *eth_colour, *wlan_colour;

   DIR *d;
   struct dirent *dir;
   FILE *fd;
   int dbm;
   char quality[7] = "";
   char profile[10] = "";

   get_interface_stats("eth0", &eth_carrier, &eth_rx, &eth_tx);
   get_interface_stats("wlan0", &wlan_carrier, &wlan_rx, &wlan_tx);

   get_interface_colour(&eth_colour, "eth0", eth_carrier);
   get_interface_colour(&wlan_colour, "wlan0", wlan_carrier);

   if((d = opendir ("/var/run/network/profiles/"))) {
      readdir(d); readdir(d); /* read . and .. */
      if ((dir = readdir(d)))
         snprintf(profile, 10, "%s ", dir->d_name);
      closedir(d);
   }

   if((fd = fopen("/proc/net/wireless", "r"))) {
      fscanf(fd, "%*[^\n]\n");
      fscanf(fd, "%*[^\n]\n");
      fscanf(fd, "wlan0: %*d %*d. -%d.", &dbm);
      fclose(fd);
      if (dbm)
         snprintf(quality, 7, "%d%s%% ", 100-(abs(dbm)-40)*100/60, Unit);
   }

   snprintf(net, 100, "%sETH0 %sRx %s%d%sMB %sTx %s%d%sMB %s %sWLAN0 %s%s%s%sRx %s%d%sMB %sTx %s%d%sMB%s", eth_colour, Title, Value, eth_rx, Unit, Title, Value, eth_tx, Unit, Sep, wlan_colour, Value, profile, quality, Title, Value, wlan_rx, Unit, Title, Value, wlan_tx, Unit, Title);
}

void get_bat_str(char *bat) {
   int energy_now, energy_full, voltage_now;
   int present;
   int level;
   char *power = "BAT";
   char *colour;

   energy_now  = read_file_int("/sys/class/power_supply/BAT0/energy_now");
   energy_full = read_file_int("/sys/class/power_supply/BAT0/energy_full");
   voltage_now = read_file_int("/sys/class/power_supply/BAT0/voltage_now");
   present     = read_file_int("/sys/class/power_supply/BAT0/present");

   if (present == 1)
      power = "AC";

   level = ((float)energy_now * 1000 / (float)voltage_now) * 100 / ((float)energy_full * 1000 / (float)voltage_now);

   get_colour(&colour, 100-level, 75, 90);
   snprintf(bat, 13, "%s%s %s%1d%s%%%s", Title, power, Value, level, Unit, Title);
}

void get_datetime(char *datetime) {
   time_t result;
   struct tm *resulttm;

   result = time(NULL);
   resulttm = localtime(&result);
   if(resulttm == NULL) {
      fprintf(stderr, "Error getting localtime.\n");
      return;
   }
   if(!strftime(datetime, 24, "%a %d-%b-%y %H:%M:%S", resulttm)) {
      fprintf(stderr, "strftime is 0.\n");
      return;
   }
}

int main(void) {
   char status[300];
   char cpu[109];
   char fan[25];
   char mem[11];
   char net[100];
   char bat[13];
   char datetime[24];

   if(!(dpy = XOpenDisplay(NULL))) {
      fprintf(stderr, "Cannot open display.\n");
      return 1;
   }

   for(;;sleep(1)) {
      get_cpu_str(cpu);
      get_fan_str(fan);
      get_mem_str(mem);
      get_net_str(net);
      get_bat_str(bat);
      get_datetime(datetime);
      snprintf(status, 300, "%s[CPU %s] [FAN %s] [MEM %s] [NET %s] [%s] %s%s", Title, cpu, fan, mem, net, bat, Clock, datetime);

      XStoreName(dpy, DefaultRootWindow(dpy), status);
      XSync(dpy, False);
   }

   XCloseDisplay(dpy);
   return 0;
}

