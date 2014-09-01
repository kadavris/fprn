#include <math.h>
#include <stdint.h>
#include <stdlib.h>

static char encoding_table[] = {
  'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
  'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
  'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
  'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
  'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
  'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
  'w', 'x', 'y', 'z', '0', '1', '2', '3',
  '4', '5', '6', '7', '8', '9', '+', '/'
};
static char *decoding_table = NULL;
static int mod_table[] = {0, 2, 1};

//================================================================================
void build_decoding_table()
{
int i;

  decoding_table = malloc(256);
  for (i = 0; i < 0x40; i++) decoding_table[(int)encoding_table[i]] = i;
}

//================================================================================
void base64_cleanup()
{
  if(decoding_table != NULL) free(decoding_table);
}

//================================================================================
char *base64_encode(const char *data, size_t input_length, size_t *output_length)
{
int i,j;
char *encoded_data;
uint32_t octet_a, octet_b, octet_c, triple;

  *output_length = (size_t) (4.0 * ceil((double) input_length / 3.0));

  encoded_data = malloc(*output_length);
  if (encoded_data == NULL) return NULL;

  for (i = 0, j = 0; i < input_length;)
  {
    octet_a = i < input_length ? data[i++] : 0;
    octet_b = i < input_length ? data[i++] : 0;
    octet_c = i < input_length ? data[i++] : 0;

    triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;

    encoded_data[j++] = encoding_table[(triple >> 3 * 6) & 0x3F];
    encoded_data[j++] = encoding_table[(triple >> 2 * 6) & 0x3F];
    encoded_data[j++] = encoding_table[(triple >> 1 * 6) & 0x3F];
    encoded_data[j++] = encoding_table[(triple >> 0 * 6) & 0x3F];
  }

  for (i = 0; i < mod_table[input_length % 3]; i++) encoded_data[*output_length - 1 - i] = '=';

  return encoded_data;
}


//================================================================================
char *base64_decode(const char *data, size_t input_length, size_t *output_length)
{
char *decoded_data;
int i,j;
uint32_t sextet_a, sextet_b, sextet_c, sextet_d, triple;

  if (decoding_table == NULL) build_decoding_table();

  if (input_length % 4 != 0) return NULL;

  *output_length = input_length / 4 * 3;
  if (data[input_length - 1] == '=') (*output_length)--;
  if (data[input_length - 2] == '=') (*output_length)--;

  decoded_data = malloc(*output_length);
  if (decoded_data == NULL) return NULL;

  for (i = 0, j = 0; i < input_length;)
  {
    sextet_a = data[i] == '=' ? 0 & i++ : decoding_table[(int)data[i++]];
    sextet_b = data[i] == '=' ? 0 & i++ : decoding_table[(int)data[i++]];
    sextet_c = data[i] == '=' ? 0 & i++ : decoding_table[(int)data[i++]];
    sextet_d = data[i] == '=' ? 0 & i++ : decoding_table[(int)data[i++]];

    triple = (sextet_a << 3 * 6)
      + (sextet_b << 2 * 6)
      + (sextet_c << 1 * 6)
      + (sextet_d << 0 * 6);

    if (j < *output_length) decoded_data[j++] = (triple >> 2 * 8) & 0xFF;
    if (j < *output_length) decoded_data[j++] = (triple >> 1 * 8) & 0xFF;
    if (j < *output_length) decoded_data[j++] = (triple >> 0 * 8) & 0xFF;
  }

  return decoded_data;
}
