/**
 * @file HUD.h
 * @author Luka Keserič
 * @date 15 July 2020
 * @brief Functions to display information to the player
 *
 */

/**
 * @defgroup HUD
 *
 * The information about the game that is displayed to the player (HUD)
 * like player lives, score, highscore, level etc.
 *
 * @{
 */


#ifndef __HUD_H__
#define __HUD_H__
/**
 * @brief Draws instructions for the game
 *
 *
 * @param what mode is the player in (either Normal or AI mode)
 * @param the difficulty of the mode (applies only to AI mode)
 * 
 */
void vDrawHelpText(char mode, char difficulty);

/**
 * @brief Displays text when the player completes a level
 *
 * Signifies that player has completed a level and gives instructions
 * on how to proceed further
 * 
 */
void vDrawNextLevel(void);

/**
 * @brief Displays text when the player looses the game
 *
 * This function is called when the player looses all the lives or 
 * when the invaders reach the ground.
 *
 * Gives instructions on how to proceed after the game is over.	
 * 
 */
void vDrawGameOver(void);

/**
 * @brief Displays the current level of the player
 *
 * @param *level a pointer to the variable where the current level is stored
 * 
 */
void vDrawLevel(unsigned short *level);

/**
 * @brief Displays the highscore and the current score of the player
 *
 * @param *score a pointer to the variable where the current score is stored
 * @param *highscore a pointer to the variable where the current highscore is stored
 *
 */
void vDrawScore(unsigned short *score, unsigned short *highscore);

/**
 * @brief Displays the lives of the player
 *
 * @param *lives a pointer to the variable where the player's lives are stored
 * @param avatar is an image that is displayed one less the amount of times the player has lives
 * next to the number of lives (3 lives -> 2 avatars displayed)
 *
 */
void vDrawLives (unsigned short *lives, image_handle_t avatar);

/** @}*/
#endif // __HUD_H__