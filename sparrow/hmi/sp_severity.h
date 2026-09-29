#ifndef SPARROW_HMI_SEVERITY_H
#define SPARROW_HMI_SEVERITY_H

/* Visual status classes shared by all HMI widgets. Order is by increasing urgency of attention. */
typedef enum {
    SP_SEVERITY_INACTIVE, /* off, idle or no data */
    SP_SEVERITY_NORMAL,
    SP_SEVERITY_INFO, /* a transition is in progress */
    SP_SEVERITY_WARNING,
    SP_SEVERITY_CRITICAL
} SpSeverity;

#endif
