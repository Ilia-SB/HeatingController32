#ifndef _LOWPASSFILTER_H
#define _LOWPASSFILTER_H

template<typename Type> class LowPassFilterFloat {
public:
  LowPassFilterFloat(Type alpha) : m_alpha(alpha) {
    m_present_value = null;
  }

  LowPassFilterFloat &set_alpha(Type value) {
    m_alpha = value;
    return *this;
  }

  //float calculate_alpha(Type sampling_frequency, Type magnitude = 0.5f);

  LowPassFilterFloat &reset(Type start){
    m_present_value = start;
    return *this;
  }

  LowPassFilterFloat &calculate(Type in){
    m_present_value = in * (m_alpha) + m_present_value * (1.0f - m_alpha);
    return *this;
  }

private:
  Type m_alpha;
  Type m_present_value;
};

#endif