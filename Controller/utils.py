def clamp(num, min_value, max_value):
   return max(min(num, max_value), min_value)

def clamp01(num):
    return clamp(num, 0.0,1.0)