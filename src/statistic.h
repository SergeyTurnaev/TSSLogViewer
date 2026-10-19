
#ifndef STATISTIC_H
#define STATISTIC_H

#define STATISTIC_FIELDS_COUNT 5

struct Statistic {
    double min;
    double max;
    double avg;
    double delta;
    double cur;
};

#endif // STATISTIC_H
